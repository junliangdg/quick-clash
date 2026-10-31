#include "desktop_flow_probe.h"
#include "application_host.h"
#include "native_proxy_client.h"
#include "network_fixture.h"
#include "platform/process_memory.h"
#ifdef Q_OS_WIN
#include "platform/windows/system_proxy.h"
#endif
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QQuickItem>
#include <QSaveFile>
#include <QSignalSpy>
#include <QSslConfiguration>
#include <QTest>
#include <QTimer>
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
bool save(const QString& path, const QJsonObject& value) {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(value).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QJsonObject load(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()
                                          : QJsonObject{};
}
bool trustFixture() {
    QFile file(QStringLiteral("tests/fixtures/network/server_cert.pem"));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QSslCertificate certificate(file.readAll());
    if (certificate.isNull())
        return false;
    auto configuration = QSslConfiguration::defaultConfiguration();
    configuration.addCaCertificate(certificate);
    QSslConfiguration::setDefaultConfiguration(configuration); // This test process only.
    return true;
}
int freePort() {
    QTcpServer server;
    return server.listen(QHostAddress::LocalHost) ? server.serverPort() : 0;
}
QQuickItem* find(QQuickItem* root, const QString& name) {
    if (root->objectName() == name)
        return root;
    for (auto* child : root->childItems())
        if (auto* found = find(child, name))
            return found;
    return nullptr;
}
QQuickItem* item(ApplicationHost& host, const QString& name) {
    return host.window() ? find(host.window()->contentItem(), name) : nullptr;
}
bool click(ApplicationHost& host, const QString& name) {
    auto* target = item(host, name);
    if (!target || !target->isVisible() || !target->isEnabled())
        return false;
    // Scroll the actual page and activate its focused control with a real key event.
    for (auto* parent = target->parentItem(); parent; parent = parent->parentItem()) {
        if (!parent->property("contentY").isValid())
            continue;
        const auto center =
            target->mapToItem(parent, QPointF(target->width() / 2, target->height() / 2));
        const auto current = parent->property("contentY").toDouble();
        const auto maximum =
            std::max(0.0, parent->property("contentHeight").toDouble() - parent->height());
        parent->setProperty("contentY",
                            std::clamp(current + center.y() - parent->height() / 2, 0.0, maximum));
    }
    QTest::qWait(30);
    QSignalSpy activated(target, SIGNAL(clicked()));
    target->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(host.window(), Qt::Key_Space);
    QTest::qWait(30);
    return activated.count() == 1;
}
bool client(const QString& flag, const QString& arguments) {
    QProcess process;
    process.start(QCoreApplication::applicationFilePath(), {flag + QLatin1Char('=') + arguments});
    if (!waitFor([&] { return process.state() == QProcess::NotRunning; }, 12000)) {
        process.kill();
        process.waitForFinished(2000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
} // namespace
int desktopFlowServer(const QString& output) {
    TlsFixture origin;
    if (!origin.initialize())
        return 1;
    ConnectFixture remote(origin.serverPort());
    if (!remote.listen(QHostAddress::LocalHost))
        return 2;
    origin.subscription = "proxies:\n- {name: fixture, type: http, server: 127.0.0.1, port: " +
                          QByteArray::number(remote.serverPort()) +
                          "}\nproxy-groups:\n- {name: upstream, type: select, proxies: [DIRECT, "
                          "fixture]}\nrules:\n- MATCH,upstream\n";
    const auto publish = [&] {
        return save(output,
                    {{"originPort", origin.serverPort()},
                     {"upstreamPort", remote.serverPort()},
                     {"subscription",
                      QStringLiteral("https://127.0.0.1:%1/subscription").arg(origin.serverPort())},
                     {"requests", origin.requests},
                     {"targets", remote.targets}});
    };
    if (!publish())
        return 3;
    QTimer updates;
    QObject::connect(&updates, &QTimer::timeout, &updates, [&] { (void)publish(); });
    updates.start(100);
    QTimer::singleShot(120000, &updates, [] { QCoreApplication::quit(); });
    return QCoreApplication::exec();
}
int desktopFlowClient() {
    const auto native = option("--desktop-flow-native");
    if (!native.isEmpty())
        return nativeProxyRequest(QStringLiteral("fixture.quickclash.invalid"), native.toUShort())
                   ? 0
                   : 1;
    const auto ports = option("--desktop-flow-client").split(QLatin1Char(','));
    if (ports.size() != 2 || !trustFixture())
        return 2;
    QNetworkAccessManager network;
    network.setProxy(
        QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), ports[0].toUShort()));
    QNetworkRequest request(
        QUrl(QStringLiteral("https://fixture.quickclash.invalid:%1/gui-proof").arg(ports[1])));
    request.setTransferTimeout(5000);
    auto* reply = network.get(request);
    return waitFor([&] { return reply->isFinished(); }, 6000) &&
                   reply->error() == QNetworkReply::NoError &&
                   reply->readAll() == "quickclash-controlled-origin"
               ? 0
               : 3;
}
bool desktopFlowProbe(ApplicationHost& host, bool reopen, QJsonObject& result) {
#ifndef Q_OS_WIN
    Q_UNUSED(host)
    Q_UNUSED(reopen)
    Q_UNUSED(result)
    return false;
#else
    auto& ui = host.state();
    const auto fixturePath = option("--desktop-flow-fixture");
    const auto fixture = load(fixturePath);
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            result.insert("flowFailure", QString::fromLatin1(message));
            std::fprintf(stderr, "GUI flow: %s\n", message);
        }
        return condition;
    };
    const auto done = [&] { return !ui.runtime().value("busy").toBool(); };
    const auto ready = [&] { return done() && ui.runtime().value("ready").toBool(); };
    const auto preferences = [&] { return ui.runtime().value("preferences").toMap(); };
    const auto selected = [&] {
        for (const auto& group : ui.groups())
            if (group.toMap().value("name") == "upstream")
                return group.toMap().value("current").toString();
        return QString{};
    };
    if (!check(!fixture.isEmpty() && trustFixture(), "controlled endpoints and test CA"))
        return false;
    host.resizeWindow({1280, 1000});
    if (!reopen) {
        auto mixed = freePort(), controller = freePort();
        while (mixed == controller && mixed > 0)
            controller = freePort();
        ui.runtimeCommand("ports", {QString::number(mixed), QString::number(controller)});
        if (!check(waitFor([&] { return preferences().value("mixedPort").toInt() == mixed; }),
                   "set isolated ports"))
            return false;
        if (!check(click(host, "navigation_subscriptions") &&
                       waitFor([&] { return item(host, "subscription_url"); }),
                   "open actual subscription page"))
            return false;
        item(host, "subscription_url")
            ->setProperty("text", fixture.value("subscription").toString());
        item(host, "import_profile_name")->setProperty("text", "GUI HTTPS fixture");
        if (!check(click(host, "add_subscription") && waitFor(ready) && ui.profiles().size() == 1,
                   "submit HTTPS subscription through its QML button"))
            return false;
        const auto firstId = ui.runtime().value("selectedId").toString();
        const auto pid = ui.runtime().value("corePid");
        result.insert("initialCorePid", QJsonValue::fromVariant(pid));
        ui.runtimeCommand("import", {QUrl::fromLocalFile(option("--desktop-fixture")).toString(),
                                     "Second local fixture"});
        if (!check(waitFor([&] { return done() && ui.profiles().size() == 2; }) &&
                       ui.runtime().value("selectedId") == firstId &&
                       ui.runtime().value("corePid") == pid,
                   "second import retains selected live session"))
            return false;
        // Select the existing card again; idempotent selection must retain the owned PID.
        auto* page = host.window()->findChild<QObject*>("subscriptions_page");
        if (!check(page && QMetaObject::invokeMethod(page, "focusRow", Q_ARG(QVariant, 0)) &&
                       waitFor([&] { return item(host, "select_subscription"); }) &&
                       click(host, "select_subscription") && ready() &&
                       ui.runtime().value("corePid") == pid,
                   "existing selected card is idempotent"))
            return false;
        if (!check(click(host, "navigation_proxies") &&
                       waitFor([&] { return item(host, "proxy_group_upstream"); }),
                   "real proxy page has imported group"))
            return false;
        if (!item(host, "proxy_member_upstream_fixture") && !click(host, "proxy_group_upstream"))
            return false;
        if (!check(waitFor([&] { return item(host, "proxy_member_upstream_fixture"); }) &&
                       click(host, "proxy_member_upstream_fixture") &&
                       waitFor([&] { return ready() && selected() == "fixture"; }),
                   "keyboard selection is read back from official core"))
            return false;
        if (!check(click(host, "navigation_home") &&
                       waitFor([&] { return item(host, "home_mode_direct"); }) &&
                       click(host, "home_mode_direct") && waitFor([&] {
                           return ready() && preferences().value("mode") == "direct";
                       }) &&
                       click(host, "home_mode_rule") &&
                       waitFor([&] { return ready() && preferences().value("mode") == "rule"; }),
                   "home mode buttons confirm direct then rule"))
            return false;
        if (!check(click(host, "navigation_settings") &&
                       waitFor([&] { return item(host, "appearance_dark"); }) &&
                       click(host, "appearance_dark") &&
                       waitFor([&] { return ui.settingsReady() && ui.themeMode() == "dark"; }) &&
                       ready() && selected() == "fixture",
                   "theme change retains Ready and node across transactional mode changes"))
            return false;
    } else {
        if (!check(waitFor(ready) && ui.profiles().size() == 2 && ui.themeMode() == "dark" &&
                       preferences().value("mode") == "rule" &&
                       !ui.runtime().value("systemProxy").toBool() &&
                       click(host, "navigation_proxies") &&
                       waitFor([&] { return selected() == "fixture"; }) &&
                       click(host, "navigation_home"),
                   "real process reopen restores profiles, mode, theme and reliable node"))
            return false;
    }
    const auto pid = ui.runtime().value("corePid");
    const auto mixed = preferences().value("mixedPort").toInt();
    const auto origin = fixture.value("originPort").toInt();
    auto count = load(fixturePath).value("targets").toArray().size();
    if (!check(client("--desktop-flow-client", QStringLiteral("%1,%2").arg(mixed).arg(origin)) &&
                   waitFor(
                       [&] { return load(fixturePath).value("targets").toArray().size() > count; }),
               "independent explicit HTTPS client traverses recorded upstream CONNECT"))
        return false;
    platform::WindowsProxySettings settings;
    const auto before = settings.read();
    if (!check(std::holds_alternative<QJsonObject>(before), "read original WinINet snapshot"))
        return false;
    struct Release {
        presentation::UiState& ui;
        ~Release() {
            ui.runtimeCommand("system_proxy", {"false"});
            (void)waitFor([&] { return !ui.runtime().value("systemProxy").toBool(); }, 5000);
        }
    } release{ui};
    if (!check(waitFor([&] { return item(host, "home_system_proxy"); }) &&
                   click(host, "home_system_proxy") &&
                   waitFor([&] { return ui.runtime().value("systemProxy").toBool(); }),
               "home switch acquires real system proxy after Ready"))
        return false;
    count = load(fixturePath).value("targets").toArray().size();
    const bool nativePassed = client("--desktop-flow-native", QString::number(origin));
    if (!check(nativePassed && waitFor([&] {
                   return load(fixturePath).value("targets").toArray().size() > count;
               }) &&
                   click(host, "home_system_proxy") &&
                   waitFor([&] { return !ui.runtime().value("systemProxy").toBool(); }) &&
                   std::holds_alternative<QJsonObject>(settings.read()) &&
                   std::get<QJsonObject>(settings.read()) == std::get<QJsonObject>(before) &&
                   ui.runtime().value("corePid") == pid,
               "independent PRECONFIG client forwards; switch restores exact original snapshot"))
        return false;
    result.insert("explicitHttps", true);
    result.insert("nativeSystemHttps", true);
    result.insert("originalProxyRestored", true);
    result.insert("nodeReadback", selected());
    result.insert("theme", ui.themeMode());
    result.insert("profiles", ui.profiles().size());
    result.insert("fixture", load(fixturePath));
    if (!reopen) {
        // One Ready home scene. The fixture server and clients are test infrastructure;
        // no product auxiliary process or privileged service participates in this session.
        const auto core = platform::processIdentity(static_cast<quint32>(pid.toUInt()));
        QJsonArray samples;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10000) {
            samples.append(QJsonObject{{"elapsedMs", timer.elapsed()},
                                       {"client", platform::currentProcessMemory()},
                                       {"core", platform::processMemory(core)}});
            QTest::qWait(100);
        }
        result.insert("readyHomeSamples", samples);
        if (!check(ready() && ui.currentPage() == "home" && ui.runtime().value("corePid") == pid &&
                       host.qmlErrorCount() == 0,
                   "ten-second Ready home scene retains core and clean QML"))
            return false;
    }
    return host.qmlErrorCount() == 0;
#endif
}
} // namespace quickclash::test
