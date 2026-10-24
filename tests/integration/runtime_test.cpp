#include "application/desktop_runtime.h"
#include "native_proxy_client.h"
#include "network_fixture.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <future>
#include <iostream>

namespace {
bool waitFor(const std::function<bool()>& condition, int limit = 15000) {
    QElapsedTimer time;
    time.start();
    while (!condition() && time.elapsed() < limit) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return condition();
}
int port() {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost))
        return 0;
    return server.serverPort();
}
bool require(bool value, const char* message, quickclash::application::DesktopRuntime& runtime) {
    if (!value)
        std::cerr << message << ": " << QJsonDocument(runtime.snapshot()).toJson().constData();
    return value;
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    quickclash::infra::JsonStore store(directory.path());
    if (std::holds_alternative<quickclash::domain::Error>(store.open()))
        return 1;
    quickclash::application::DesktopRuntime runtime(store);
    runtime.initialize();
    // Opt-in real endpoint check: input and raw content never appear in diagnostics.
    if (app.arguments().contains(QStringLiteral("--subscription-probe"))) {
        std::string source;
        std::getline(std::cin, source);
        runtime.addSubscription(QString::fromStdString(source), QStringLiteral("Probe"));
        const bool completed =
            waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }, 30000);
        const auto state = runtime.snapshot();
        std::cout << QJsonDocument(QJsonObject{{QStringLiteral("completed"), completed},
                                               {QStringLiteral("profiles"),
                                                state.value("profiles").toArray().size()},
                                               {QStringLiteral("error"), state.value("error")}})
                         .toJson(QJsonDocument::Compact)
                         .constData();
        return completed ? 0 : 1;
    }
    int mixed = port(), controller = port();
    while (controller == mixed)
        controller = port();
    runtime.setPorts(mixed, controller);
    runtime.importFile(QUrl::fromLocalFile(QDir::current().absoluteFilePath(QStringLiteral(
                                               "tests/fixtures/mvp/inline_valid.yaml")))
                           .toString(),
                       QStringLiteral("Fixture A"));
    if (!require(waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }) &&
                     runtime.snapshot().value("profiles").toArray().size() == 1,
                 "import", runtime))
        return 2;
    const auto id =
        runtime.snapshot().value("profiles").toArray()[0].toObject().value("id").toString();
    runtime.selectProfile(id);
    if (!require(waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }) &&
                     runtime.snapshot().value("selectedId").toString() == id,
                 "stopped selection", runtime))
        return 3;
    runtime.start(false);
    if (!require(waitFor([&] {
                     return runtime.snapshot().value("ready").toBool() ||
                            (!runtime.snapshot().value("busy").toBool() &&
                             !runtime.snapshot().value("error").toString().isEmpty());
                 }) &&
                     runtime.snapshot().value("ready").toBool(),
                 "core ready", runtime))
        return 4;
    if (!require(waitFor([&] { return !runtime.snapshot().value("groups").toArray().isEmpty(); }),
                 "groups", runtime))
        return 5;
    runtime.selectProxy(QString::fromUtf8("代理 / A%?#"), QStringLiteral("DIRECT"));
    if (!require(waitFor([&] {
                     for (const auto& value : runtime.snapshot().value("groups").toArray()) {
                         if (value.toObject().value("name") == QString::fromUtf8("代理 / A%?#") &&
                             value.toObject().value("current") == QStringLiteral("DIRECT") &&
                             !runtime.snapshot().value("busy").toBool())
                             return true;
                     }
                     return false;
                 }),
                 "encoded selector readback", runtime))
        return 6;
    runtime.setMode(QStringLiteral("global"));
    if (!require(waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }) &&
                     runtime.snapshot().value("ready").toBool() &&
                     runtime.snapshot().value("preferences").toObject().value("mode") ==
                         QStringLiteral("global"),
                 "mode", runtime))
        return 7;
    runtime.stop();
    if (!require(!runtime.snapshot().value("ready").toBool(), "stop", runtime))
        return 8;
    quickclash::application::DesktopRuntime reopened(store);
    reopened.initialize();
    if (!require(reopened.snapshot().value("selectedId").toString() == id &&
                     reopened.snapshot().value("preferences").toObject().value("mode") ==
                         QStringLiteral("global"),
                 "reopen", reopened))
        return 9;
    reopened.start(false);
    if (!require(waitFor([&] { return reopened.snapshot().value("ready").toBool(); }), "restart",
                 reopened))
        return 10;
    if (!require(waitFor([&] {
                     if (reopened.snapshot().value("busy").toBool())
                         return false;
                     for (const auto& group : reopened.snapshot().value("groups").toArray()) {
                         if (group.toObject().value("name") == QString::fromUtf8("代理 / A%?#") &&
                             group.toObject().value("current") == QStringLiteral("DIRECT"))
                             return true;
                     }
                     return false;
                 }),
                 "persisted node identity restored", reopened))
        return 29;
    reopened.previewConfiguration();
    if (!require(reopened.snapshot().value("effectivePreview").toString().contains("[redacted]") &&
                     !reopened.snapshot()
                          .value("effectivePreview")
                          .toString()
                          .contains("proxy.example.invalid"),
                 "masked effective config", reopened))
        return 30;
    reopened.stop();
    quickclash::test::TlsFixture origin;
    if (!origin.initialize())
        return 11;
    quickclash::test::ConnectFixture remote(origin.serverPort());
    if (!remote.listen(QHostAddress::LocalHost))
        return 12;
    origin.subscription = "proxies:\n  - {name: fixture, type: http, server: 127.0.0.1, port: " +
                          QByteArray::number(remote.serverPort()) +
                          "}\nproxy-groups:\n  - {name: upstream, type: select, proxies: [fixture, "
                          "DIRECT]}\nrules:\n  - MATCH,upstream\n";
    const auto source =
        QStringLiteral("https://127.0.0.1:%1/subscription?token=public-fixture-token")
            .arg(origin.serverPort());
    reopened.addSubscription(QStringLiteral("  ") + source + QStringLiteral("\r\n"),
                             QStringLiteral("Fixture B HTTPS"));
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("profiles").toArray().size() == 2 &&
                     origin.lastUserAgent.startsWith("clash.meta/1.19.33") &&
                     origin.lastAccept.contains("application/yaml"),
                 "https subscription", reopened))
        return 13;
    const auto second = reopened.snapshot().value("profiles").toArray()[1].toObject();
    reopened.selectProfile(second.value("id").toString());
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }),
                 "second selection", reopened))
        return 14;
    reopened.setMode(QStringLiteral("rule"));
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }), "rule mode",
                 reopened))
        return 15;
    reopened.start(false);
    if (!require(waitFor([&] { return reopened.snapshot().value("ready").toBool(); }),
                 "second start", reopened))
        return 16;
    QNetworkAccessManager client;
    client.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"),
                                  static_cast<quint16>(mixed)));
    auto* response = client.get(QNetworkRequest(QUrl(
        QStringLiteral("https://fixture.quickclash.invalid:%1/proof").arg(origin.serverPort()))));
    if (!require(waitFor([&] { return response->isFinished(); }) &&
                     response->error() == QNetworkReply::NoError &&
                     response->readAll() == QByteArrayLiteral("quickclash-controlled-origin") &&
                     !remote.targets.isEmpty(),
                 "real domain CONNECT via controlled upstream", reopened))
        return 17;
    response->deleteLater();
    reopened.testWebsite(QStringLiteral("https://fixture.quickclash.invalid:%1/website-test")
                             .arg(origin.serverPort()));
    if (!require(waitFor([&] {
                     return !reopened.snapshot().value("testResult").toString().isEmpty();
                 }) &&
                     reopened.snapshot()
                         .value("testResult")
                         .toString()
                         .startsWith(QStringLiteral("HTTP 200")),
                 "website current proxy", reopened))
        return 18;
    origin.invalidSubscription = true;
    reopened.updateProfile(second.value("id").toString());
    if (!require(
            waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                reopened.snapshot().value("profiles").toArray()[1].toObject().value("sourceHash") ==
                    second.value("sourceHash") &&
                reopened.snapshot().value("ready").toBool(),
            "invalid update preserves running configuration", reopened))
        return 19;
    origin.invalidSubscription = false;
    const auto validSubscription = origin.subscription;
    for (const auto& body : {QByteArrayLiteral("<html>Sign in</html>"),
                             QByteArrayLiteral("c3M6Ly9wdWJsaWMtZml4dHVyZQ=="), QByteArray{}}) {
        origin.subscription = body;
        reopened.updateProfile(second.value("id").toString());
        if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                         reopened.snapshot()
                                 .value("profiles")
                                 .toArray()[1]
                                 .toObject()
                                 .value("sourceHash") == second.value("sourceHash") &&
                         reopened.snapshot().value("ready").toBool() &&
                         reopened.snapshot().value("error").toString() ==
                             (body.isEmpty() ? QStringLiteral("EmptySubscription")
                                             : QStringLiteral("UnsupportedSubscriptionFormat")),
                     "unsupported subscription preserves running configuration", reopened))
            return 40;
    }
    origin.subscription = validSubscription;
    origin.requiredUserAgent = "QuickClash-Fixture";
    const auto previousSource =
        reopened.snapshot().value("profiles").toArray()[1].toObject().value("secretRef");
    reopened.changeProfileSource(
        second.value("id").toString(), source + QStringLiteral("&revision=2"),
        QStringLiteral("{\"User-Agent\":\"QuickClash-Fixture\",\"Accept\":\"text/yaml\"}"));
    if (!require(
            waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                reopened.snapshot().value("profiles").toArray()[1].toObject().value("secretRef") !=
                    previousSource &&
                reopened.snapshot().value("ready").toBool() &&
                origin.lastUserAgent == "QuickClash-Fixture" && origin.lastAccept == "text/yaml",
            "validated source replacement preserves running artifact", reopened))
        return 33;
    reopened.renameProfile(second.value("id").toString(), QStringLiteral("Renamed"));
    reopened.deleteProfile(id);
    if (!require(reopened.snapshot().value("profiles").toArray().size() == 1, "catalog management",
                 reopened))
        return 20;
    // Occupied candidate port must restore the previous confirmed session and preferences.
    QTcpServer occupied;
    if (!occupied.listen(QHostAddress::LocalHost))
        return 21;
    reopened.setPorts(occupied.serverPort(), controller);
    if (!require(
            waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }, 20000) &&
                reopened.snapshot().value("ready").toBool() &&
                reopened.snapshot().value("preferences").toObject().value("mixedPort").toInt() ==
                    mixed,
            "occupied port compensation", reopened))
        return 22;
    const auto crashedPid = static_cast<quint32>(reopened.snapshot().value("corePid").toInt());
    if (!quickclash::test::terminateOwnedFixture(crashedPid) ||
        !require(waitFor([&] {
                     return reopened.snapshot().value("ready").toBool() &&
                            reopened.snapshot().value("corePid").toInt() !=
                                static_cast<int>(crashedPid) &&
                            !reopened.snapshot().value("busy").toBool();
                 }),
                 "bounded crash recovery", reopened))
        return 24;
    for (int i = 0; i < 20; ++i)
        reopened.reconcile();
    QElapsedTimer merged;
    merged.start();
    if (!require(waitFor([&] { return merged.elapsed() >= 1800; }), "merged reconcile", reopened))
        return 25;
    bool nativePassed = false;
    if (app.arguments().contains(QStringLiteral("--native-system-proxy"))) {
        quickclash::platform::WindowsProxySettings os;
        const auto before = os.read();
        if (!std::holds_alternative<QJsonObject>(before))
            return 26;
        (void)reopened.setSystemProxy(true);
        if (!require(reopened.snapshot().value("systemProxy").toBool(), "native lease", reopened))
            return 27;
        const auto firstLease = os.read();
        (void)reopened.setSystemProxy(true);
        const auto repeatedLease = os.read();
        if (!require(std::holds_alternative<QJsonObject>(firstLease) &&
                         std::holds_alternative<QJsonObject>(repeatedLease) &&
                         std::get<QJsonObject>(firstLease) == std::get<QJsonObject>(repeatedLease),
                     "native idempotent acquire", reopened))
            return 31;
        const auto migratedPort = port();
        reopened.setPorts(migratedPort, controller);
        if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }, 20000) &&
                         reopened.snapshot().value("ready").toBool() &&
                         reopened.snapshot().value("systemProxy").toBool() &&
                         reopened.snapshot()
                                 .value("preferences")
                                 .toObject()
                                 .value("mixedPort")
                                 .toInt() == migratedPort,
                     "native port migration", reopened))
            return 32;
        const auto beforeCount = remote.targets.size();
        auto native = std::async(std::launch::async, [&] {
            return quickclash::test::nativeProxyRequest(
                QStringLiteral("fixture.quickclash.invalid"), origin.serverPort());
        });
        const bool finished = waitFor(
            [&] {
                return native.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
            },
            20000);
        nativePassed = finished && native.get() && remote.targets.size() > beforeCount;
        reopened.stop(quickclash::domain::StopReason::AppExit);
        quickclash::application::DesktopRuntime intentReopened(store);
        intentReopened.initialize();
        if (!require(intentReopened.snapshot()
                             .value("preferences")
                             .toObject()
                             .value("systemProxyDesired")
                             .toBool() &&
                         !intentReopened.snapshot().value("ready").toBool(),
                     "exit retains persisted intent without auto start", intentReopened))
            return 36;
        (void)intentReopened.stopProxy(quickclash::domain::StopReason::UserStop);
        if (!require(!intentReopened.snapshot()
                          .value("preferences")
                          .toObject()
                          .value("systemProxyDesired")
                          .toBool(),
                     "user stop clears persisted intent", intentReopened))
            return 37;
        const auto after = os.read();
        if (!require(nativePassed && std::holds_alternative<QJsonObject>(after) &&
                         std::get<QJsonObject>(before) == std::get<QJsonObject>(after),
                     "PRECONFIG HTTPS and exact original restore", reopened))
            return 28;
    }
    reopened.stop();
    const auto stoppedOperation = reopened.stopProxy(quickclash::domain::StopReason::UserStop);
    if (!require(!stoppedOperation.isNull() &&
                     reopened.snapshot().value("operation").toObject().value("state") ==
                         QStringLiteral("Succeeded"),
                 "stop facade terminal state", reopened))
        return 35;
    reopened.deleteProfile(second.value("id").toString());
    if (!require(reopened.snapshot().value("profiles").toArray().isEmpty() &&
                     reopened.snapshot().value("selectedId").toString().isEmpty() &&
                     !reopened.snapshot().value("ready").toBool(),
                 "delete selected last profile", reopened))
        return 34;
    QJsonObject evidence{
        {QStringLiteral("result"), QStringLiteral("Pass")},
        {QStringLiteral("originRequests"), origin.requests},
        {QStringLiteral("upstreamConnectTargets"), remote.targets},
        {QStringLiteral("systemProxy"), nativePassed
                                            ? QStringLiteral("native_preconfig_pass_restored")
                                            : QStringLiteral("untouched")},
        {QStringLiteral("checks"),
         QJsonArray{"local_import", "isolated_validation", "owned_ready",
                    "encoded_selection_readback", "mode_transaction", "stop_restart",
                    "https_subscription", "domain_connect", "website_current_proxy",
                    "invalid_update_preserves_old", "catalog_rename_delete",
                    "occupied_port_compensation", "bounded_crash_recovery", "merged_reconcile",
                    "persisted_node_restore", "redacted_preview", "source_replacement",
                    "delete_last_profile"}}};
    // Exercise new protocols through the same official isolated validator and running session.
    QTcpServer health;
    int healthRequests = 0;
    if (!health.listen(QHostAddress::LocalHost))
        return 49;
    QObject::connect(&health, &QTcpServer::newConnection, &health, [&] {
        while (auto* socket = health.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &healthRequests] {
                const auto request = socket->readAll();
                if (request.startsWith("GET ") || request.startsWith("HEAD ")) {
                    ++healthRequests;
                    socket->write("HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: "
                                  "close\r\n\r\n");
                    socket->disconnectFromHost();
                }
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    QFile publicProtocols(QStringLiteral("tests/fixtures/mvp/mainstream_valid.yaml"));
    if (!publicProtocols.open(QIODevice::ReadOnly))
        return 50;
    auto publicBytes = publicProtocols.readAll();
    publicBytes.replace("https://example.invalid/health",
                        "http://127.0.0.1:" + QByteArray::number(health.serverPort()) + "/health");
    publicBytes.replace("lazy: true", "lazy: false");
    publicBytes.replace("proxies: [DIRECT, REJECT]", "proxies: [REJECT, DIRECT]");
    publicBytes.replace("proxies: [DIRECT, fallback_fixture]", "proxies: [REJECT, DIRECT]");
    const auto mainstreamPath = directory.filePath(QStringLiteral("mainstream.yaml"));
    QFile localProtocols(mainstreamPath);
    if (!localProtocols.open(QIODevice::WriteOnly) ||
        localProtocols.write(publicBytes) != publicBytes.size())
        return 51;
    localProtocols.close();
    reopened.importFile(QUrl::fromLocalFile(mainstreamPath).toString(),
                        QStringLiteral("Synthetic protocols"));
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("profiles").toArray().size() == 1,
                 "mainstream official isolated validation", reopened))
        return 41;
    const auto mainstreamId =
        reopened.snapshot().value("profiles").toArray()[0].toObject().value("id").toString();
    reopened.selectProfile(mainstreamId);
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }),
                 "mainstream selection", reopened))
        return 42;
    reopened.start(false);
    if (!require(waitFor([&] {
                     return reopened.snapshot().value("ready").toBool() &&
                            !reopened.snapshot().value("busy").toBool();
                 }),
                 "mainstream Ready", reopened))
        return 43;
    if (!require(waitFor([&] { return healthRequests > 0; }), "Mihomo automatic health checks",
                 reopened))
        return 52;
    reopened.refresh();
    if (!require(waitFor([&] {
                     bool selectedAutomatic = false, selectedFallback = false;
                     for (const auto& entry : reopened.snapshot().value("groups").toArray()) {
                         const auto group = entry.toObject();
                         if (group.value("name") == "automatic_fixture")
                             selectedAutomatic = group.value("current") == "DIRECT";
                         if (group.value("name") == "fallback_fixture")
                             selectedFallback = group.value("current") == "DIRECT";
                     }
                     return selectedAutomatic && selectedFallback;
                 }),
                 "automatic groups bypass unhealthy first member", reopened))
        return 53;
    bool automatic = false, fallback = false, manual = false;
    for (const auto& entry : reopened.snapshot().value("groups").toArray()) {
        const auto group = entry.toObject();
        const auto type = group.value("type").toString();
        if (type == "URLTest")
            automatic =
                !group.value("canSelect").toBool() && !group.value("current").toString().isEmpty();
        if (type == "Fallback")
            fallback =
                !group.value("canSelect").toBool() && !group.value("current").toString().isEmpty();
        if (type == "Selector")
            manual = group.value("canSelect").toBool();
    }
    if (!require(automatic && fallback && manual, "group capability projection", reopened))
        return 44;
    reopened.selectProxy(QStringLiteral("automatic_fixture"), QStringLiteral("DIRECT"));
    if (!require(reopened.snapshot().value("error") == "ProxyGroupAutomatic" &&
                     !reopened.snapshot().value("busy").toBool(),
                 "automatic group mutation rejected", reopened))
        return 45;
    reopened.selectProxy(QStringLiteral("primary_fixture"), QStringLiteral("hy2_fixture"));
    if (!require(waitFor([&] {
                     for (const auto& entry : reopened.snapshot().value("groups").toArray())
                         if (entry.toObject().value("name") == "primary_fixture" &&
                             entry.toObject().value("current") == "hy2_fixture" &&
                             !reopened.snapshot().value("busy").toBool())
                             return true;
                     return false;
                 }),
                 "mainstream selector readback", reopened))
        return 46;
    const auto previousPid = reopened.snapshot().value("corePid");
    reopened.importFile(
        QUrl::fromLocalFile(QDir::current().absoluteFilePath(QStringLiteral(
                                "tests/fixtures/mvp/compatibility_unsupported.yaml")))
            .toString(),
        QStringLiteral("Rejected"));
    if (!require(!reopened.snapshot().value("configIssues").toArray().isEmpty() &&
                     reopened.snapshot().value("corePid") == previousPid &&
                     reopened.snapshot().value("ready").toBool() &&
                     reopened.snapshot().value("profiles").toArray().size() == 1,
                 "compatibility issues preserve running candidate", reopened))
        return 47;
    reopened.stop();
    reopened.start(false);
    if (!require(waitFor([&] {
                     return reopened.snapshot().value("ready").toBool() &&
                            !reopened.snapshot().value("busy").toBool();
                 }) &&
                     reopened.snapshot().value("configIssues").toArray().isEmpty(),
                 "protocol restore ignores automatic groups and clears stale diagnostics",
                 reopened))
        return 48;
    reopened.stop();
    evidence.insert(QStringLiteral("mainstreamChecks"),
                    QJsonArray{"official_isolated_validation", "owned_ready", "core_health_checks",
                               "automatic_groups_bypass_reject", "automatic_group_projection",
                               "automatic_mutation_rejected", "protocol_selection_readback",
                               "bad_candidate_preserves_running", "protocol_stop_restart"});
    QSaveFile evidenceFile(QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("p2_runtime_evidence.json")));
    if (!evidenceFile.open(QIODevice::WriteOnly) ||
        evidenceFile.write(QJsonDocument(evidence).toJson()) < 0 || !evidenceFile.commit())
        return 23;
    std::cout << "Import, isolated -t, stopped selection, owned Ready, encoded selection readback, "
                 "mode, stop and restart passed.\n";
    return 0;
}
