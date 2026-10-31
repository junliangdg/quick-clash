#include "desktop_probe.h"
#include "application_host.h"
#include "desktop_flow_probe.h"
#include "desktop_proxy_probe.h"
#include "platform/process_memory.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QSGRendererInterface>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <algorithm>
#include <cstdio>
#include <functional>

namespace quickclash::test {
namespace {
QString option(const char* name) {
    const auto prefix = QString::fromLatin1(name) + QLatin1Char('=');
    for (const auto& argument : QCoreApplication::arguments())
        if (argument.startsWith(prefix))
            return argument.mid(prefix.size());
    return {};
}
bool waitFor(const std::function<bool()>& condition, int milliseconds = 20000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < milliseconds)
        QTest::qWait(10);
    return condition();
}
int freePort() {
    QTcpServer server;
    return server.listen(QHostAddress::LocalHost) ? server.serverPort() : 0;
}
bool save(const QString& path, const QJsonObject& value) {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(value).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
} // namespace
QString desktopProbeSettings(const QString& root) {
    const QFileInfo directory(root);
    QFile marker(QDir(root).filePath(QStringLiteral(".quickclash_desktop_probe")));
    if (root.isEmpty() || !directory.isAbsolute() || !directory.isDir() || directory.isSymLink() ||
        !marker.open(QIODevice::ReadOnly) ||
        marker.readAll() != QByteArrayLiteral("QuickClash isolated desktop probe\n")) {
        std::fputs("Desktop probe requires an absolute marked test directory.\n", stderr);
        return {};
    }
    return QDir(directory.canonicalFilePath()).filePath(QStringLiteral("ui.ini"));
}
int runDesktopProbe(ApplicationHost& host) {
    auto& ui = host.state();
    const auto phase = option("--desktop-phase");
    const auto output = option("--desktop-output");
    QJsonObject result{{QStringLiteral("phase"), phase},
                       {QStringLiteral("passed"), false},
                       {QStringLiteral("guiPid"), QCoreApplication::applicationPid()}};
    const auto snapshot = [&](const QString& stage) {
        const auto runtime = ui.runtime();
        result.insert(QStringLiteral("stage"), stage);
        for (const auto* key :
             {"ready", "busy", "corePid", "error", "selectedId", "selectionGeneration",
              "systemProxy", "proxyConflict", "tunState", "preferences", "reconciliation"})
            result.insert(QString::fromLatin1(key),
                          QJsonValue::fromVariant(runtime.value(QString::fromLatin1(key))));
        result.insert(QStringLiteral("engineGeneration"), host.engineGeneration());
        result.insert(QStringLiteral("activeDemands"), ui.activeDemandCount());
        result.insert(QStringLiteral("activeStreams"), ui.activeStreamCount());
        result.insert(QStringLiteral("qmlErrors"), host.qmlErrorCount());
        result.insert(QStringLiteral("pageReady"), host.contentReady());
        result.insert(QStringLiteral("trayAvailable"), ui.trayAvailable());
        result.insert(QStringLiteral("graphicsApi"),
                      host.window()
                          ? static_cast<int>(host.window()->rendererInterface()->graphicsApi())
                          : -1);
        return save(output, result);
    };
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            result.insert(QStringLiteral("failure"), QString::fromLatin1(message));
            (void)snapshot(QStringLiteral("failed"));
            std::fprintf(stderr, "Desktop probe: %s\n", message);
        }
        return condition;
    };
    const auto ready = [&] {
        return ui.runtime().value(QStringLiteral("ready")).toBool() &&
               !ui.runtime().value(QStringLiteral("busy")).toBool();
    };
    if (!check(!output.isEmpty() &&
                   (phase == "import" || phase == "lifecycle" || phase == "hold" ||
                    phase == "reopen" || phase == "repair" || phase == "proxy" || phase == "flow" ||
                    phase == "flow_reopen" || phase == "traffic"),
               "explicit output and supported phase required") ||
        !check(waitFor([&] { return ui.settingsReady() && host.contentReady(); }),
               "real settings worker and QML content"))
        return 170;
    if (phase == "flow" || phase == "flow_reopen") {
        if (!check(desktopFlowProbe(host, phase == "flow_reopen", result),
                   "real GUI subscription, selection, mode and forwarding"))
            return 187;
        result.insert(QStringLiteral("passed"), true);
        return snapshot(QStringLiteral("completed")) ? 0 : 185;
    }
    if (phase != "reopen") {
        auto mixed = freePort(), controller = freePort();
        while (mixed == controller && mixed != 0)
            controller = freePort();
        if (!check(mixed > 0 && controller > 0, "available loopback ports"))
            return 171;
        ui.runtimeCommand(QStringLiteral("ports"),
                          {QString::number(mixed), QString::number(controller)});
        if (!check(waitFor([&] {
                       return ui.runtime()
                                  .value(QStringLiteral("preferences"))
                                  .toMap()
                                  .value(QStringLiteral("mixedPort"))
                                  .toInt() == mixed;
                   }),
                   "port repair persists without a running core"))
            return 172;
        ui.runtimeCommand(QStringLiteral("import"),
                          {QUrl::fromLocalFile(option("--desktop-fixture")).toString(),
                           QStringLiteral("Desktop loopback fixture")});
    }
    if (phase == "repair") {
        if (!check(waitFor([&] {
                       return !ui.runtime().value(QStringLiteral("error")).toString().isEmpty();
                   }) &&
                       !ready() && ui.runtime().value(QStringLiteral("corePid")).toInt() == 0,
                   "missing core reports an error without starting a session"))
            return 173;
        for (const auto* page : {"subscriptions", "settings", "logs", "home"})
            if (!check(ui.navigate(QString::fromLatin1(page)) &&
                           waitFor([&] { return host.contentReady(); }),
                       "repair routes remain available"))
                return 174;
        ui.runtimeCommand(QStringLiteral("system_proxy"), {QStringLiteral("false")});
        result.insert(QStringLiteral("passed"), true);
        return snapshot(QStringLiteral("repair_available")) ? 0 : 175;
    }
    if (!check(waitFor(ready) && ui.profiles().size() == 1 &&
                   !ui.runtime().value(QStringLiteral("systemProxy")).toBool() &&
                   ui.runtime().value(QStringLiteral("tunState")) == QStringLiteral("Off"),
               "local import or reopen reaches Ready without network switches"))
        return 176;
    const auto corePid = ui.runtime().value(QStringLiteral("corePid"));
    const auto selected = ui.runtime().value(QStringLiteral("selectedId"));
    const auto selection = ui.runtime().value(QStringLiteral("selectionGeneration"));
    if (!check(corePid.toInt() > 0 && !selected.toString().isEmpty(), "owned core and selection"))
        return 177;
    if (phase == "traffic") {
        host.window()->hide();
        host.window()->show();
        if (!check(QTest::qWaitForWindowExposed(host.window(), 5000),
                   "traffic probe starts with an exposed native window"))
            return 188;
        result.insert(QStringLiteral("trafficGraphicsApi"),
                      static_cast<int>(host.window()->rendererInterface()->graphicsApi()));
        const auto sampleTime = [&] {
            return ui.telemetry().value("traffic").toMap().value("time").toString().toLongLong();
        };
        QJsonArray trafficSamples;
        qint64 previousTime = 0;
        qint64 maximumAge = 0;
        qint64 maximumInterval = 0;
        QObject trafficObserver;
        QObject::connect(&ui, &presentation::UiState::telemetryChanged, &trafficObserver, [&] {
            const auto time = sampleTime();
            if (time <= 0 || time == previousTime)
                return;
            const auto age = QDateTime::currentMSecsSinceEpoch() - time;
            const auto interval = previousTime > 0 ? time - previousTime : 0;
            maximumAge = std::max(maximumAge, age);
            maximumInterval = std::max(maximumInterval, interval);
            trafficSamples.append(QJsonObject{
                {"time", QString::number(time)}, {"ageMs", age}, {"intervalMs", interval}});
            previousTime = time;
        });
        if (!check(waitFor([&] { return sampleTime() > 0; }, 5000), "first real traffic sample"))
            return 188;
        const auto generation = ui.dataDemands().generations.value("traffic");
        const auto coreIdentity = platform::processIdentity(corePid.toInt());
        QJsonArray memory;
        QJsonArray stages;
        qint64 stageStart = sampleTime();
        for (int tick = 0; tick < 100; ++tick) {
            if (tick == 30 || tick == 60 || tick == 90) {
                const auto history = ui.uploadHistory();
                if (!check(sampleTime() > stageStart && history.size() == 60 &&
                               history.at(58).isValid() && history.at(59).isValid(),
                           "real samples remain consecutive throughout each foreground state"))
                    return 188;
                stages.append(QJsonObject{{"tick", tick}, {"time", QString::number(sampleTime())}});
                stageStart = sampleTime();
                if (tick == 30)
                    (void)ui.navigate(QStringLiteral("settings"));
                else if (tick == 60)
                    ui.requestMinimize();
                else if (!host.showWindow() || !ui.navigate(QStringLiteral("home")))
                    return 188;
            }
            QTest::qWait(100);
            if (!check(
                    ui.dataDemands().generations.value("traffic") == generation &&
                        ui.dataDemands().active.contains("traffic") &&
                        (tick < 60 || tick >= 90 ||
                         (!ui.uiVisible() && ui.dataDemands().active == QSet<QString>{"traffic"})),
                    "routes and native minimization share one traffic lease"))
                return 188;
            memory.append(QJsonObject{{"tick", tick},
                                      {"client", platform::currentProcessMemory()},
                                      {"core", platform::processMemory(coreIdentity)}});
        }
        result.insert(QStringLiteral("memorySamples"), memory);
        result.insert(QStringLiteral("trafficStages"), stages);
        result.insert(QStringLiteral("trafficGeneration"), QString::number(generation));
        result.insert(QStringLiteral("trafficSamples"), trafficSamples);
        result.insert(QStringLiteral("maximumTrafficAgeMs"), maximumAge);
        result.insert(QStringLiteral("maximumTrafficIntervalMs"), maximumInterval);
        QObject::disconnect(&ui, nullptr, &trafficObserver, nullptr);
        if (!check(trafficSamples.size() >= 9 && maximumAge < 150 && maximumInterval < 1500,
                   "traffic arrives promptly without polling delay or missing samples"))
            return 188;
        const auto latestHistory = ui.uploadHistory();
        host.window()->hide();
        if (!check(ui.dataDemands().active.isEmpty() && !ui.uploadHistory().constLast().isValid(),
                   "native hiding releases traffic and clears history"))
            return 188;
        QTest::qWait(1200);
        if (!check(!ui.uploadHistory().constLast().isValid() && host.showWindow() &&
                       ui.dataDemands().generations.value("traffic") > generation &&
                       waitFor([&] { return sampleTime() > 0; }, 5000),
                   "hidden stream stays stopped and resumes with a fresh lease"))
            return 188;
        ui.requestMinimize();
        host.unloadWindow();
        if (!check(!host.window() && ui.dataDemands().active.isEmpty() &&
                       !ui.uploadHistory().constLast().isValid() && ready() &&
                       ui.runtime().value("corePid") == corePid,
                   "unloading a minimized window stops telemetry and preserves the core"))
            return 188;
        result.insert(QStringLiteral("historyPoints"), latestHistory.size());
        result.insert(QStringLiteral("passed"), true);
        return snapshot(QStringLiteral("completed")) ? 0 : 185;
    }
    if (phase == "hold") {
        if (!snapshot(QStringLiteral("holding")))
            return 178;
        // The controller kills this whole GUI process, not a worker surrogate.
        QTimer::singleShot(45000, &host, [] { QCoreApplication::exit(179); });
        return QCoreApplication::exec();
    }
    if (phase == "lifecycle") {
        int received = 0;
        QTcpServer origin;
        QObject::connect(&origin, &QTcpServer::newConnection, &origin, [&] {
            while (origin.hasPendingConnections()) {
                auto* socket = origin.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &received] {
                    auto request = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", request);
                    if (!request.contains("\r\n\r\n") || socket->property("replied").toBool())
                        return;
                    socket->setProperty("replied", true);
                    ++received;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 17\r\nConnection: "
                                  "close\r\n\r\nquickclash-hidden");
                    socket->disconnectFromHost();
                });
            }
        });
        const auto transfer = [&] {
            const auto before = received;
            QTcpSocket client;
            client.connectToHost(QStringLiteral("127.0.0.1"),
                                 static_cast<quint16>(ui.runtime()
                                                          .value(QStringLiteral("preferences"))
                                                          .toMap()
                                                          .value(QStringLiteral("mixedPort"))
                                                          .toInt()));
            if (!waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; }, 3000))
                return false;
            client.write(QStringLiteral("GET http://127.0.0.1:%1/lifecycle HTTP/1.1\r\n"
                                        "Host: 127.0.0.1:%1\r\nConnection: close\r\n\r\n")
                             .arg(origin.serverPort())
                             .toUtf8());
            if (!waitFor([&] { return client.state() == QAbstractSocket::UnconnectedState; }, 5000))
                return false;
            const auto response = client.readAll();
            return response.startsWith("HTTP/1.1 200") &&
                   response.endsWith("\r\n\r\nquickclash-hidden") && received == before + 1;
        };
        if (!check(ui.trayAvailable() && origin.listen(QHostAddress::LocalHost) && transfer(),
                   "native tray and actual loopback HTTP forwarding"))
            return 180;
        (void)ui.navigate(QStringLiteral("connections"));
        ui.setFilter(QStringLiteral("connections"), QStringLiteral("lifecycle"));
        if (!check(waitFor([&] { return ui.activeDemandCount() > 0; }), "live page demand"))
            return 181;
        const auto generation = host.engineGeneration();
        const QPointer<QQuickWindow> oldWindow = host.window();
        ui.setUnloadWhenHidden(true);
        host.hideWindow(true);
        if (!check(waitFor([&] { return !host.window(); }, 7000) && oldWindow.isNull() &&
                       ui.activeDemandCount() == 0 && ui.activeStreamCount() == 0 &&
                       ui.dataDemands().active.isEmpty() && ready() &&
                       ui.runtime().value(QStringLiteral("corePid")) == corePid && transfer(),
                   "five-second hidden unload releases consumers and retains forwarding"))
            return 182;
        result.insert(QStringLiteral("hiddenTransfer"), true);
        if (!check(
                host.showWindow() &&
                    waitFor([&] { return host.contentReady() && ui.activeDemandCount() > 0; }) &&
                    host.engineGeneration() == generation + 1 &&
                    ui.currentPage() == QStringLiteral("connections") &&
                    ui.readState(QStringLiteral("connections")).value(QStringLiteral("filter")) ==
                        QStringLiteral("lifecycle") &&
                    ui.runtime().value(QStringLiteral("corePid")) == corePid &&
                    ui.runtime().value(QStringLiteral("selectedId")) == selected &&
                    ui.runtime().value(QStringLiteral("selectionGeneration")) == selection &&
                    ready() && transfer(),
                "one QML rebuild restores the route and filter with the same core"))
            return 183;
        result.insert(QStringLiteral("forwardedRequests"), received);
    }
    if (phase == "proxy" &&
        !check(desktopProxyProbe(host, result), "native external proxy conflict and exact cleanup"))
        return 186;
    if (!check(host.qmlErrorCount() == 0, "no QML load errors"))
        return 184;
    result.insert(QStringLiteral("passed"), true);
    return snapshot(QStringLiteral("completed")) ? 0 : 185;
}
} // namespace quickclash::test
