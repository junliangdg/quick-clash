#include "desktop_probe.h"
#include "application_host.h"
#include "desktop_flow_probe.h"
#include "desktop_proxy_probe.h"
#include "infra/core/service_client.h"
#include "platform/process_memory.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <utility>

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
QQuickItem* visualItem(QQuickItem* parent, const QString& name) {
    if (!parent)
        return nullptr;
    if (parent->objectName() == name)
        return parent;
    for (auto* child : parent->childItems())
        if (auto* found = visualItem(child, name))
            return found;
    return nullptr;
}
QJsonObject serviceStatus() {
    infra::ServiceClient client;
    domain::Result<QJsonObject> response = domain::Error{};
    bool finished = false;
    client.command(QStringLiteral("status"), {}, [&](domain::Result<QJsonObject> value) {
        response = std::move(value);
        finished = true;
    });
    if (!waitFor([&] { return finished; }, 5000)) {
        client.close();
        return {{QStringLiteral("error"), QStringLiteral("StatusTimeout")}};
    }
    if (const auto* value = std::get_if<QJsonObject>(&response))
        return *value;
    return {{QStringLiteral("error"), std::get<domain::Error>(response).code}};
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
                    phase == "proxy_hold" || phase == "proxy_reopen" || phase == "tun_gui" ||
                    phase == "reopen" || phase == "repair" || phase == "proxy" || phase == "flow" ||
                    phase == "flow_reopen" || phase == "traffic" || phase == "startup" ||
                    phase == "traffic_hidden_resource"),
               "explicit output and supported phase required") ||
        !check(waitFor([&] { return ui.settingsReady() && host.contentReady(); }),
               "real settings worker and QML content"))
        return 170;
    if (phase == "startup") {
        // Reopen an existing isolated store, including multi-profile/Unicode
        // fixtures. Never import, alter ports, or enable a network switch here.
        if (!check(waitFor(ready) && ui.profileModel()->count() > 0 &&
                       !ui.runtime().value("systemProxy").toBool() &&
                       ui.runtime().value("tunState") == QStringLiteral("Off"),
                   "existing profiles reopen ready with network takeover disabled"))
            return 196;
        const auto corePid = ui.runtime().value("corePid");
        const auto selected = ui.runtime().value("selectedId");
        const auto profiles = ui.profileModel()->count();
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (const auto* page : {"proxies", "subscriptions", "settings", "home"}) {
                if (!check(ui.navigate(QString::fromLatin1(page)) &&
                               waitFor([&] { return host.contentReady(); }),
                           "startup routes remain responsive"))
                    return 196;
                QTest::qWait(100);
            }
            const auto generation = host.engineGeneration();
            host.unloadWindow();
            if (!check(!host.window() && host.showWindow() && waitFor([&] {
                    return host.contentReady();
                }) && host.engineGeneration() == generation + 1 &&
                           ready() && ui.runtime().value("corePid") == corePid &&
                           ui.runtime().value("selectedId") == selected &&
                           ui.profileModel()->count() == profiles && host.qmlErrorCount() == 0,
                       "window rebuild preserves ready session and profile identity"))
                return 196;
            QTest::qWait(100);
        }
        result.insert("profileCount", profiles);
        result.insert("rebuildCycles", 3);
        result.insert(QStringLiteral("passed"), true);
        return snapshot(QStringLiteral("completed")) ? 0 : 185;
    }
    if (phase == "flow" || phase == "flow_reopen") {
        if (!check(desktopFlowProbe(host, phase == "flow_reopen", result),
                   "real GUI subscription, selection, mode and forwarding"))
            return 187;
        result.insert(QStringLiteral("passed"), true);
        return snapshot(QStringLiteral("completed")) ? 0 : 185;
    }
    if (phase == "proxy_reopen") {
        if (!check(ui.profiles().size() == 1 && desktopProxyReopenProbe(host, result) &&
                       host.qmlErrorCount() == 0,
                   "reopen restores the interrupted native proxy lease"))
            return 197;
        result.insert(QStringLiteral("passed"), true);
        return snapshot(QStringLiteral("completed")) ? 0 : 185;
    }
    if (phase == "tun_gui") {
        const auto initial = serviceStatus();
        result.insert(QStringLiteral("serviceInitial"), initial);
        if (!check(initial.value("clean").toBool() && !initial.value("coreRunning").toBool(),
                   "TUN GUI probe requires an idle, clean service"))
            return 199;
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
    if (phase == "traffic_hidden_resource") {
        if (!check(
                waitFor(
                    [&] { return !ui.telemetry().value("traffic").toMap().value("time").isNull(); },
                    5000),
                "traffic sample before resource comparison"))
            return 188;
        host.unloadWindow();
        if (!check(!host.window() && ready(), "resource comparison unloads a ready window"))
            return 188;
        const auto identity = platform::processIdentity(corePid.toInt());
        const auto start = ui.telemetry().value("traffic").toMap().value("time").toString();
        if (!snapshot(QStringLiteral("hidden_sampling")))
            return 185;
        QJsonArray memory;
        for (int tick = 0; tick < 12; ++tick) {
            QTest::qWait(1000);
            memory.append(QJsonObject{{"tick", tick},
                                      {"client", platform::currentProcessMemory()},
                                      {"core", platform::processMemory(identity)}});
        }
        result.insert(QStringLiteral("memorySamples"), memory);
        result.insert(QStringLiteral("firstTrafficTime"), start);
        result.insert(QStringLiteral("lastTrafficTime"),
                      ui.telemetry().value("traffic").toMap().value("time").toString());
        result.insert(QStringLiteral("passed"), true);
        return snapshot(QStringLiteral("completed")) ? 0 : 185;
    }
    if (phase == "tun_gui") {
        host.resizeWindow({1280, 1000});
        host.window()->show();
        host.window()->requestActivate();
        if (!check(QTest::qWaitForWindowExposed(host.window(), 5000) &&
                       QTest::qWaitForWindowActive(host.window(), 3000),
                   "native TUN probe requires an active window"))
            return 199;
        QPointer<QQuickItem> control;
        QJsonArray stages;
        const auto toggle = [&](const QString& surface, bool desired) {
            if (!control || !control->isEnabled())
                return false;
            QSignalSpy clicked(control, SIGNAL(clicked()));
            if (!clicked.isValid())
                return false;
            const auto before = ui.runtime();
            const auto point = control->mapToScene(control->boundingRect().center()).toPoint();
            for (int press = 0; press < 6; ++press)
                QTest::mouseClick(host.window(), Qt::LeftButton, Qt::NoModifier, point);
            const auto immediate = clicked.count();
            const bool pending = waitFor([&] {
                const auto state = ui.runtime();
                return state.value("busy").toBool() &&
                       state.value(desired ? "tunPending" : "tunDisabling").toBool();
            });
            const bool disabled = pending && control && !control->isEnabled();
            const auto atBusy = clicked.count();
            if (disabled)
                for (int press = 0; press < 6; ++press)
                    QTest::mouseClick(host.window(), Qt::LeftButton, Qt::NoModifier, point);
            const auto duringBusy = clicked.count() - atBusy;
            const bool completed = waitFor(
                [&] {
                    const auto state = ui.runtime();
                    return state.value("ready").toBool() && !state.value("busy").toBool() &&
                           state.value("tunState") == (desired ? "On" : "Off") &&
                           state.value("coreExecutionMode") == (desired ? "Service" : "Local") &&
                           state.value("corePid").toInt() > 0 &&
                           state.value("corePid") != before.value("corePid") &&
                           state.value("preferences").toMap().value("tunDesired").toBool() ==
                               desired;
                },
                30000);
            const auto settled = ui.runtime();
            QTest::qWait(500);
            const auto after = ui.runtime();
            const bool stable =
                completed && after.value("ready").toBool() && !after.value("busy").toBool() &&
                after.value("tunState") == settled.value("tunState") &&
                after.value("corePid") == settled.value("corePid") &&
                after.value("epoch") == settled.value("epoch") &&
                after.value("preferences").toMap().value("tunDesired").toBool() == desired;
            stages.append(QJsonObject{{"surface", surface},
                                      {"desired", desired},
                                      {"immediateClicks", static_cast<int>(immediate)},
                                      {"busyObserved", pending},
                                      {"disabledWhileBusy", disabled},
                                      {"busyClicks", static_cast<int>(duringBusy)},
                                      {"completed", completed},
                                      {"stableAfter500Ms", stable},
                                      {"pidBefore", before.value("corePid").toInt()},
                                      {"pidAfter", after.value("corePid").toInt()},
                                      {"error", after.value("error").toString()}});
            return immediate >= 1 && immediate <= 6 && disabled && duringBusy == 0 && stable;
        };
        bool passed = true;
        for (const auto& surface : {QStringLiteral("home"), QStringLiteral("settings")}) {
            if (!passed || !ui.navigate(surface)) {
                passed = false;
                break;
            }
            if (surface == QStringLiteral("settings"))
                ui.setSettingsCategory(1);
            const auto name = surface == QStringLiteral("home") ? QStringLiteral("home_tun")
                                                                : QStringLiteral("settings_tun");
            if (!waitFor([&] {
                    control = visualItem(host.window()->contentItem(), name);
                    return control && control->isVisible() && control->isEnabled();
                })) {
                result.insert(QStringLiteral("missingTunSurface"), surface);
                passed = false;
                break;
            }
            // Bring each switch into the visible viewport before native mouse events.
            for (auto* parent = control->parentItem(); parent; parent = parent->parentItem()) {
                if (!parent->property("contentY").isValid())
                    continue;
                const auto center = control->mapToItem(parent, control->boundingRect().center());
                const auto current = parent->property("contentY").toDouble();
                const auto maximum =
                    std::max(0.0, parent->property("contentHeight").toDouble() - parent->height());
                parent->setProperty(
                    "contentY",
                    std::clamp(current + center.y() - parent->height() / 2, 0.0, maximum));
            }
            QTest::qWait(30);
            passed = toggle(surface, true) && toggle(surface, false);
        }
        result.insert(QStringLiteral("tunGuiStages"), stages);
        ui.runtimeCommand(QStringLiteral("stop"));
        const bool stopped = waitFor([&] {
            const auto state = ui.runtime();
            return !state.value("ready").toBool() && !state.value("busy").toBool() &&
                   state.value("corePid").toInt() == 0;
        });
        const auto final = serviceStatus();
        result.insert(QStringLiteral("serviceFinal"), final);
        if (!check(passed && stopped && final.value("clean").toBool() &&
                       !final.value("coreRunning").toBool(),
                   "rapid native TUN clicks serialize and leave the service clean"))
            return 199;
        result.insert(QStringLiteral("passed"), true);
        return snapshot(QStringLiteral("completed")) ? 0 : 185;
    }
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
        const auto beforeHide = sampleTime();
        host.window()->hide();
        if (!check(ui.dataDemands().active == QSet<QString>{"traffic"} &&
                       ui.uploadHistory().constLast().isValid() &&
                       ui.dataDemands().generations.value("traffic") == generation,
                   "native hiding retains only the shared traffic stream"))
            return 188;
        if (!check(waitFor([&] { return sampleTime() > beforeHide; }, 5000) && host.showWindow() &&
                       ui.uploadHistory().constLast().isValid() &&
                       ui.dataDemands().generations.value("traffic") == generation,
                   "hidden traffic advances and remains available on restore"))
            return 188;
        ui.requestMinimize();
        host.unloadWindow();
        const auto beforeUnload = sampleTime();
        if (!check(!host.window() && ui.dataDemands().active == QSet<QString>{"traffic"} &&
                       waitFor([&] { return sampleTime() > beforeUnload; }, 5000) && ready() &&
                       ui.uploadHistory().constLast().isValid() &&
                       ui.dataDemands().generations.value("traffic") == generation &&
                       ui.runtime().value("corePid") == corePid,
                   "unloaded window continues receiving traffic from the same core"))
            return 188;
        if (!check(host.showWindow() && waitFor([&] { return host.contentReady(); }) &&
                       ui.uploadHistory().constLast().isValid(),
                   "recreated window can immediately read background history"))
            return 188;
        host.unloadWindow();
        ui.runtimeCommand(QStringLiteral("stop"));
        if (!check(waitFor([&] {
                       const auto state = ui.runtime();
                       return !state.value("ready").toBool() && !state.value("busy").toBool() &&
                              state.value("status") == QStringLiteral("Stopped");
                   }) &&
                       ui.dataDemands().active.isEmpty() && ui.trafficHasHistory() &&
                       ui.upload() == QStringLiteral("0.0 KiB/s"),
                   "confirmed hidden stop retains history and shows zero rate"))
            return 188;
        QTest::qWait(2200);
        const auto stoppedHistory = ui.uploadHistory();
        const auto oldPoints = std::count_if(stoppedHistory.cbegin(), stoppedHistory.cend(),
                                             [](const auto& point) { return point.isValid(); });
        if (!check(stoppedHistory.constLast().isValid() &&
                       stoppedHistory.constLast().toDouble() == 0 && oldPoints >= 4,
                   "stopped history advances with zeros while the window is unloaded"))
            return 188;
        if (!check(host.showWindow() && waitFor([&] { return host.contentReady(); }),
                   "stopped dashboard can be rebuilt"))
            return 188;
        auto* scale = visualItem(host.window()->contentItem(), QStringLiteral("traffic_scale"));
        auto* empty = visualItem(host.window()->contentItem(), QStringLiteral("traffic_empty"));
        if (!check(scale && scale->isVisible() && empty && !empty->isVisible() &&
                       ui.upload() == QStringLiteral("0.0 KiB/s"),
                   "stopped dashboard keeps the retained chart visible without an empty overlay"))
            return 188;
        host.unloadWindow();
        ui.runtimeCommand(QStringLiteral("start"));
        if (!check(waitFor(ready) && waitFor([&] { return sampleTime() > 0; }, 5000) &&
                       ui.runtime().value("corePid") != corePid && ui.trafficHasHistory(),
                   "a new core appends samples to the retained in-memory history"))
            return 188;
        const auto restartedHistory = ui.uploadHistory();
        if (!check(std::count_if(restartedHistory.cbegin(), restartedHistory.cend(),
                                 [](const auto& point) { return point.isValid(); }) >= oldPoints &&
                       restartedHistory.constLast().isValid(),
                   "the latest sample follows the earlier stopped history"))
            return 188;
        if (!check(host.showWindow() && waitFor([&] { return host.contentReady(); }) &&
                       host.qmlErrorCount() == 0,
                   "the rebuilt dashboard displays the retained history"))
            return 188;
        result.insert(QStringLiteral("historyPoints"), latestHistory.size());
        result.insert(QStringLiteral("passed"), true);
        return snapshot(QStringLiteral("completed")) ? 0 : 185;
    }
    if (phase == "hold" || phase == "proxy_hold") {
        if (phase == "proxy_hold" &&
            !check(desktopProxyHoldProbe(host, result), "native GUI applies a durable lease"))
            return 198;
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
        if (!check(waitFor([&] { return !host.window(); }, 4000) && oldWindow.isNull() &&
                       ui.activeDemandCount() == 0 && ui.activeStreamCount() == 1 &&
                       ui.dataDemands().active == QSet<QString>{"traffic"} && ready() &&
                       ui.runtime().value(QStringLiteral("corePid")) == corePid && transfer(),
                   "queued tray unload releases consumers promptly and retains forwarding"))
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
        // Submit an edit and close on the same UI turn, while the real runtime owns
        // the transaction. Reopening must attach to the result without resubmitting.
        if (!check(ui.navigate(QStringLiteral("rules")) && waitFor([&] {
                       return host.window()->findChild<QObject*>("custom_rules_dialog") &&
                              host.window()->findChild<QObject*>("user_rules_editor");
                   }),
                   "rule editor page for in-flight tray close"))
            return 189;
        auto* rulesDialog = host.window()->findChild<QObject*>("custom_rules_dialog");
        auto* rulesEditor = host.window()->findChild<QObject*>("user_rules_editor");
        if (!check(QMetaObject::invokeMethod(rulesDialog, "open") && waitFor([&] {
                       return !ui.userRules().value("revision").toString().isEmpty() &&
                              !rulesEditor->property("baseRevision").toString().isEmpty() &&
                              !ui.runtime().value("busy").toBool();
                   }),
                   "real rule editor data loaded"))
            return 190;
        const auto previousAttempt = ui.userRules().value("attempt").toString().toULongLong();
        auto* value = host.window()->findChild<QObject*>("user_rule_value");
        if (!check(
                value &&
                    QMetaObject::invokeMethod(rulesEditor, "edit", Q_ARG(QVariant, QVariant(-1))) &&
                    value->setProperty("text", "lifecycle.example") &&
                    QMetaObject::invokeMethod(rulesEditor, "saveRuleForm") &&
                    rulesEditor->property("globalDirty").toBool() &&
                    QMetaObject::invokeMethod(rulesEditor, "confirm") &&
                    rulesEditor->property("saving").toBool(),
                "submit real rule edit before hiding"))
            return 191;
        const auto savingGeneration = host.engineGeneration();
        const QPointer<QQuickWindow> savingWindow = host.window();
        host.hideWindow(true);
        if (!check(waitFor([&] { return !host.window(); }, 4000) && savingWindow.isNull() &&
                       ui.userRulesDraft().value("saving").toBool(),
                   "tray unload preserves in-flight rule save identity"))
            return 192;
        if (!check(host.showWindow() && host.engineGeneration() == savingGeneration + 1 &&
                       waitFor([&] {
                           return host.contentReady() &&
                                  host.window()->findChild<QObject*>("custom_rules_dialog") &&
                                  host.window()->findChild<QObject*>("user_rules_editor");
                       }),
                   "reopen after in-flight rule save"))
            return 193;
        rulesDialog = host.window()->findChild<QObject*>("custom_rules_dialog");
        rulesEditor = host.window()->findChild<QObject*>("user_rules_editor");
        QSignalSpy completed(rulesEditor, SIGNAL(saveCompleted()));
        const bool opened = completed.isValid() && QMetaObject::invokeMethod(rulesDialog, "open");
        const bool reconciled = opened && waitFor([&] {
                                    return completed.size() == 1 &&
                                           !rulesEditor->property("saving").toBool() &&
                                           !rulesEditor->property("dirty").toBool() &&
                                           !rulesDialog->property("visible").toBool() &&
                                           ui.userRulesDraft().isEmpty() && ready();
                                });
        // Successful completion closes the dialog and immediately releases the
        // scoped result. Reopen it like a user to inspect a fresh snapshot instead
        // of observing the same signal that QML can invalidate reentrantly.
        const bool reopened = reconciled && QMetaObject::invokeMethod(rulesDialog, "open");
        const bool reloaded =
            reopened && waitFor([&] {
                const auto view = ui.userRules();
                return view.value("result") == QStringLiteral("Saved") &&
                       view.value("attempt").toString().toULongLong() > previousAttempt;
            });
        const auto view = ui.userRules();
        const auto observedAttempt = view.value("attempt").toString().toULongLong();
        int savedRuleCopies = 0;
        for (const auto& item : view.value("global").toList())
            if (item.toMap().value("value") == QStringLiteral("lifecycle.example"))
                ++savedRuleCopies;
        const bool singleAttempt = observedAttempt == previousAttempt + 1;
        const bool forwarded =
            reconciled && reloaded && savedRuleCopies == 1 && singleAttempt && transfer();
        if (!forwarded) {
            result.insert("ruleSaveDiagnostics",
                          QJsonObject{{"opened", opened},
                                      {"reconciled", reconciled},
                                      {"reopened", reopened},
                                      {"reloaded", reloaded},
                                      {"completedSignals", completed.size()},
                                      {"savedRuleCopies", savedRuleCopies},
                                      {"previousAttempt", QString::number(previousAttempt)},
                                      {"observedAttempt", QString::number(observedAttempt)},
                                      {"lastResult", view.value("result").toString()},
                                      {"editorSaving", rulesEditor->property("saving").toBool()},
                                      {"editorDirty", rulesEditor->property("dirty").toBool()},
                                      {"dialogVisible", rulesDialog->property("visible").toBool()},
                                      {"draftEmpty", ui.userRulesDraft().isEmpty()},
                                      {"ready", ready()},
                                      {"singleAttempt", singleAttempt}});
        }
        if (!check(forwarded, "one real rule save completes across unload and forwarding resumes"))
            return 194;
        if (!check(QMetaObject::invokeMethod(rulesDialog, "close") && waitFor([&] {
                       return !rulesDialog->property("visible").toBool() &&
                              !ui.dataDemands().active.contains(QStringLiteral("user_rules"));
                   }),
                   "closing the verified rule editor releases its scoped demand"))
            return 195;
        result.insert(QStringLiteral("hiddenRuleSave"), true);
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
