#include "application/desktop_runtime.h"
#include "native_proxy_client.h"
#include "network_fixture.h"
#include "subscription_memory_probe.h"
#include "subscription_probe.h"
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
class MemoryProxySettings final : public quickclash::platform::ProxySettings {
  public:
    QJsonObject value{{QStringLiteral("flags"), 1},
                      {QStringLiteral("server"), QString{}},
                      {QStringLiteral("bypass"), QString{}},
                      {QStringLiteral("pac"), QString{}}};
    quickclash::domain::Result<QJsonObject> read() override {
        return value;
    }
    quickclash::domain::CommandResult write(const QJsonObject& next) override {
        value = next;
        return std::monostate{};
    }
};
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
    if (app.arguments().contains(QStringLiteral("--update-memory-probe")))
        return quickclash::test::subscriptionMemoryProbe();
    QTemporaryDir directory;
    quickclash::infra::JsonStore store(directory.path());
    if (std::holds_alternative<quickclash::domain::Error>(store.open()))
        return 1;
    quickclash::application::DesktopRuntime runtime(store);
    runtime.initialize();
    if (app.arguments().contains(QStringLiteral("--subscription-e2e")))
        return quickclash::test::subscriptionProbe(runtime, store);
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
    if (!require(runtime.snapshot().value("selectedId").toString() == id &&
                     !runtime.snapshot().value("groups").toArray().isEmpty() &&
                     !runtime.snapshot().value("ready").toBool(),
                 "first import selects profile and exposes offline groups", runtime))
        return 100;
    runtime.selectProxy(QString::fromUtf8("代理 / A%?#"), QStringLiteral("DIRECT"));
    bool offlineSelected = false;
    for (const auto& value : runtime.snapshot().value("groups").toArray())
        offlineSelected |= value.toObject().value("name") == QString::fromUtf8("代理 / A%?#") &&
                           value.toObject().value("current") == QStringLiteral("DIRECT");
    if (!require(offlineSelected, "select node before starting core", runtime))
        return 101;
    runtime.selectProfile(id);
    if (!require(waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }) &&
                     runtime.snapshot().value("selectedId").toString() == id,
                 "stopped selection", runtime))
        return 3;
    runtime.start(false);
    if (!require(waitFor([&] {
                     return (runtime.snapshot().value("ready").toBool() &&
                             !runtime.snapshot().value("busy").toBool()) ||
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
    if (!require(!runtime.snapshot().value("ready").toBool() &&
                     !runtime.snapshot().value("groups").toArray().isEmpty(),
                 "stop retains offline nodes", runtime))
        return 8;
    auto subscriptionTime = QDateTime::currentDateTimeUtc();
    quickclash::application::DesktopRuntime reopened(store, nullptr, nullptr,
                                                     [&] { return subscriptionTime; });
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
    auto second = reopened.snapshot().value("profiles").toArray()[1].toObject();
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
    const auto ordinaryPid = reopened.snapshot().value("corePid");
    reopened.setTun(true);
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("ready").toBool() &&
                     reopened.snapshot().value("corePid") == ordinaryPid &&
                     reopened.snapshot().value("tunState") == QStringLiteral("Off"),
                 "unavailable service preserves ordinary proxy", reopened))
        return 181;
    reopened.testWebsite(QStringLiteral("https://127.0.0.1:%1/direct").arg(origin.serverPort()),
                         QStringLiteral("Direct"));
    if (!require(waitFor([&] {
                     return reopened.snapshot()
                                .value("features")
                                .toObject()
                                .value("websiteTest")
                                .toObject()
                                .value("state") != QStringLiteral("Running");
                 }) &&
                     reopened.snapshot()
                             .value("features")
                             .toObject()
                             .value("websiteTest")
                             .toObject()
                             .value("path") == QStringLiteral("Direct"),
                 "explicit direct request reports selected path", reopened))
        return 182;
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
    const auto subscriptionId = second.value("id").toString();
    reopened.configureSubscription(subscriptionId, 60, true);
    reopened.selectProxy(QStringLiteral("upstream"), QStringLiteral("fixture"));
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }),
                 "selection before update", reopened))
        return 60;
    const auto updatePid = reopened.snapshot().value("corePid");
    const auto requestsBefore = origin.requests.size();
    origin.subscription += "# updated public fixture\n";
    origin.subscription.replace(
        "proxy-groups:",
        "  - {name: added, type: http, server: 127.0.0.1, port: 1}\nproxy-groups:");
    origin.subscription.replace("proxies: [fixture, DIRECT]", "proxies: [fixture, DIRECT, added]");
    subscriptionTime = subscriptionTime.addSecs(61 * 60);
    for (int i = 0; i < 20; ++i)
        reopened.reconcile();
    if (!require(
            waitFor([&] {
                const auto state = reopened.snapshot();
                return !state.value("busy").toBool() && state.value("ready").toBool() &&
                       state.value("corePid") != updatePid &&
                       state.value("profiles").toArray()[1].toObject().value("lastUpdateResult") ==
                           "Updated";
            }) &&
                origin.requests.size() == requestsBefore + 1,
            "overdue schedule coalesces and applies selected update", reopened))
        return 61;
    bool selectedRestored = false;
    for (const auto& group : reopened.snapshot().value("groups").toArray())
        if (group.toObject().value("name") == "upstream" &&
            group.toObject().value("current") == "fixture")
            selectedRestored = true;
    reopened.testWebsite(QStringLiteral("https://fixture.quickclash.invalid:%1/after-update")
                             .arg(origin.serverPort()));
    if (!require(
            selectedRestored && waitFor([&] {
                return reopened.snapshot().value("testResult").toString().startsWith("HTTP 200");
            }),
            "selection and forwarding survive automatic apply", reopened))
        return 62;
    const auto unchangedPid = reopened.snapshot().value("corePid");
    reopened.updateProfile(subscriptionId);
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("corePid") == unchangedPid,
                 "unchanged update does not restart core", reopened))
        return 63;
    origin.invalidSubscription = true;
    subscriptionTime = subscriptionTime.addSecs(61 * 60);
    reopened.reconcile();
    if (!require(waitFor([&] {
                     return reopened.snapshot()
                                .value("profiles")
                                .toArray()[1]
                                .toObject()
                                .value("lastUpdateResult") == "Failed";
                 }) &&
                     reopened.snapshot().value("corePid") == unchangedPid &&
                     QDateTime::fromString(reopened.snapshot()
                                               .value("profiles")
                                               .toArray()[1]
                                               .toObject()
                                               .value("nextUpdateAt")
                                               .toString(),
                                           Qt::ISODate) > subscriptionTime,
                 "rejected background update preserves core and backs off", reopened))
        return 64;
    origin.invalidSubscription = false;
    reopened.configureSubscription(subscriptionId, 0, false);
    origin.subscription += "# manual apply\n";
    reopened.updateProfile(subscriptionId);
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("corePid") == unchangedPid &&
                     reopened.snapshot()
                         .value("profiles")
                         .toArray()[1]
                         .toObject()
                         .value("pendingApply")
                         .toBool(),
                 "auto apply can be disabled", reopened))
        return 65;
    reopened.selectProfile(subscriptionId);
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     !reopened.snapshot()
                          .value("profiles")
                          .toArray()[1]
                          .toObject()
                          .value("pendingApply")
                          .toBool(),
                 "manual use confirms downloaded candidate", reopened))
        return 66;
    reopened.updateProfile(subscriptionId);
    reopened.stop();
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot()
                             .value("profiles")
                             .toArray()[1]
                             .toObject()
                             .value("lastUpdateResult") == "Cancelled" &&
                     !reopened.snapshot().value("ready").toBool(),
                 "stop cancels pending update without restart", reopened))
        return 67;
    reopened.start(false);
    if (!require(waitFor([&] {
                     return reopened.snapshot().value("ready").toBool() &&
                            !reopened.snapshot().value("busy").toBool();
                 }),
                 "restart after cancelled update", reopened))
        return 68;
    const auto validSubscription = origin.subscription;
    second = reopened.snapshot().value("profiles").toArray()[1].toObject();
    // Fail only the candidate's listener, then let compensation restart the old session.
    reopened.configureSubscription(subscriptionId, 0, true);
    QTcpServer candidatePortBlocker;
    bool candidateBlocked = false;
    const auto beforeRejectedApply = reopened.snapshot().value("selectionGeneration");
    const auto applyFailure =
        QObject::connect(&reopened, &quickclash::application::DesktopRuntime::changed, &app,
                         [&](const QJsonObject& state) {
                             if (state.value("status") == "Starting" && !candidateBlocked) {
                                 candidateBlocked = candidatePortBlocker.listen(
                                     QHostAddress::LocalHost, static_cast<quint16>(mixed));
                             }
                             if (candidateBlocked && !state.value("error").toString().isEmpty())
                                 candidatePortBlocker.close();
                         });
    origin.subscription += "# rejected candidate start\n";
    reopened.updateProfile(subscriptionId);
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }, 20000) &&
                     candidateBlocked && reopened.snapshot().value("ready").toBool() &&
                     reopened.snapshot().value("selectionGeneration") == beforeRejectedApply &&
                     reopened.snapshot()
                             .value("profiles")
                             .toArray()[1]
                             .toObject()
                             .value("lastUpdateResult") == "Failed" &&
                     reopened.snapshot()
                         .value("profiles")
                         .toArray()[1]
                         .toObject()
                         .value("pendingApply")
                         .toBool(),
                 "automatic apply failure compensates to confirmed session", reopened))
        return 69;
    QObject::disconnect(applyFailure);
    reopened.setMode(QStringLiteral("rule"));
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot()
                         .value("profiles")
                         .toArray()[1]
                         .toObject()
                         .value("pendingApply")
                         .toBool(),
                 "mode change cannot mark unapplied download as applied", reopened))
        return 70;
    reopened.configureSubscription(subscriptionId, 0, false);
    second = reopened.snapshot().value("profiles").toArray()[1].toObject();
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
        reopened.configureSubscription(subscriptionId, 60, true);
        origin.subscription += "# native system proxy update\n";
        reopened.updateProfile(subscriptionId);
        if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                         reopened.snapshot().value("ready").toBool() &&
                         reopened.snapshot().value("systemProxy").toBool() &&
                         !reopened.snapshot()
                              .value("profiles")
                              .toArray()[0]
                              .toObject()
                              .value("pendingApply")
                              .toBool(),
                     "native lease restored after subscription auto apply", reopened))
            return 71;
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
    {
        quickclash::application::DesktopRuntime restoredSchedule(store);
        restoredSchedule.initialize();
        const auto policy = restoredSchedule.snapshot().value("profiles").toArray()[0].toObject();
        if (!require(policy.value("updateIntervalMinutes").toInt() == (nativePassed ? 60 : 0) &&
                         policy.value("autoApply").toBool() == nativePassed &&
                         !restoredSchedule.snapshot().value("ready").toBool(),
                     "subscription policy survives app reopen without starting core",
                     restoredSchedule))
            return 72;
    }
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
    reopened.importFile(QUrl::fromLocalFile(QDir::current().absoluteFilePath(QStringLiteral(
                                                "tests/fixtures/mvp/dns_supported.yaml")))
                            .toString(),
                        QStringLiteral("Managed DNS and Geo"));
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("profiles").toArray().size() == 2,
                 "official isolated DNS and Geo validation", reopened))
        return 54;
    reopened.selectProfile(
        reopened.snapshot().value("profiles").toArray()[1].toObject().value("id").toString());
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }),
                 "DNS selection", reopened))
        return 55;
    reopened.start(false);
    if (!require(waitFor([&] { return reopened.snapshot().value("ready").toBool(); }),
                 "DNS Geo Ready", reopened))
        return 56;
    reopened.stop();
    reopened.importFile(QUrl::fromLocalFile(QDir::current().absoluteFilePath(QStringLiteral(
                                                "tests/fixtures/mvp/advanced_supported.yaml")))
                            .toString(),
                        QStringLiteral("Advanced DNS and TUIC"));
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("profiles").toArray().size() == 3,
                 "advanced official isolated validation", reopened))
        return 110;
    reopened.selectProfile(
        reopened.snapshot().value("profiles").toArray()[2].toObject().value("id").toString());
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }),
                 "advanced selection", reopened))
        return 111;
    reopened.start(false);
    if (!require(waitFor([&] {
                     return reopened.snapshot().value("ready").toBool() &&
                            !reopened.snapshot().value("busy").toBool();
                 }),
                 "advanced Ready", reopened))
        return 112;
    reopened.setDemands({QStringLiteral("rules")});
    reopened.featureCommand(QStringLiteral("rules"), {});
    if (!require(waitFor([&] { return !reopened.snapshot().value("rules").toArray().isEmpty(); }),
                 "rules API", reopened))
        return 113;
    const auto originalRules = reopened.snapshot().value("rules");
    reopened.setDemands({});
    if (!require(reopened.snapshot().value("rules").toArray().isEmpty() &&
                     !reopened.snapshot()
                          .value("dataLifecycle")
                          .toObject()
                          .value("publishedChannels")
                          .toArray()
                          .contains("rules"),
                 "last rules consumer releases worker cache and publication baseline", reopened))
        return 126;
    reopened.featureCommand(QStringLiteral("rules"), {});
    QCoreApplication::processEvents();
    if (!require(reopened.snapshot().value("rules").toArray().isEmpty(),
                 "diagnostic command does not bypass rules demand", reopened))
        return 127;
    reopened.setDemands({QStringLiteral("rules")});
    if (!require(waitFor([&] { return reopened.snapshot().value("rules") == originalRules; }),
                 "same-session rules reacquisition refetches unchanged complete data", reopened))
        return 128;
    reopened.featureCommand(QStringLiteral("flush_dns"), {});
    if (!require(waitFor([&] {
                     return !reopened.snapshot()
                                 .value("features")
                                 .toObject()
                                 .value("dnsResult")
                                 .toString()
                                 .isEmpty();
                 }) &&
                     reopened.snapshot().value("features").toObject().value("dnsResult") ==
                         "Cleared",
                 "DNS cache API", reopened))
        return 114;
    reopened.setDemands(
        {QStringLiteral("traffic"), QStringLiteral("resources"), QStringLiteral("connections")});
    if (!require(waitFor([&] {
                     const auto f = reopened.snapshot().value("features").toObject();
                     return f.contains("traffic") && f.contains("memory") &&
                            f.contains("connectionSample");
                 }),
                 "live feature demand", reopened))
        return 115;
    reopened.setDemands({});
    reopened.stop();
    reopened.featureCommand(QStringLiteral("backup"), {QStringLiteral("confirmed")});
    const auto archiveUrl =
        reopened.snapshot().value("features").toObject().value("backupUrl").toString();
    if (!require(!archiveUrl.isEmpty(), "offline backup", reopened))
        return 116;
    QFile archive(QUrl(archiveUrl).toLocalFile());
    if (!archive.open(QIODevice::ReadOnly))
        return 117;
    auto archiveObject = QJsonDocument::fromJson(archive.readAll()).object();
    if (archiveObject.contains("secretRef") ||
        archiveObject.value("profiles").toArray().size() != 3)
        return 118;
    reopened.featureCommand(QStringLiteral("restore_backup"),
                            {archiveUrl, QStringLiteral("confirmed")});
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }, 20000) &&
                     reopened.snapshot().value("profiles").toArray().size() == 6,
                 "atomic backup restore", reopened))
        return 119;
    auto corrupt = archiveObject.value("profiles").toArray();
    auto firstEntry = corrupt[0].toObject();
    firstEntry.insert(QStringLiteral("sha256"), QStringLiteral("bad"));
    corrupt[0] = firstEntry;
    archiveObject.insert(QStringLiteral("profiles"), corrupt);
    const auto badArchivePath = directory.filePath(QStringLiteral("bad_backup.json"));
    QFile badArchive(badArchivePath);
    if (!badArchive.open(QIODevice::WriteOnly))
        return 120;
    badArchive.write(QJsonDocument(archiveObject).toJson());
    badArchive.close();
    reopened.featureCommand(
        QStringLiteral("restore_backup"),
        {QUrl::fromLocalFile(badArchivePath).toString(), QStringLiteral("confirmed")});
    if (!require(reopened.snapshot().value("error") == "InvalidBackup" &&
                     reopened.snapshot().value("profiles").toArray().size() == 6,
                 "bad backup leaves catalog intact", reopened))
        return 121;
    evidence.insert(QStringLiteral("p3FeatureChecks"),
                    QJsonArray{"advanced_official_validation", "advanced_ready", "rules_api",
                               "dns_cache_flush", "live_traffic_connections_resources",
                               "backup_restore_atomic", "bad_backup_preserves_catalog"});
    origin.requiredUserAgent = "clash.meta/";
    origin.payloads.insert(
        "/provider_nodes",
        "proxies:\n  - {name: managed_fixture, type: http, server: 127.0.0.1, port: 9}\n");
    origin.payloads.insert("/provider_rules",
                           "payload: ['DOMAIN,example.invalid', 'DOMAIN-SUFFIX,example.com']\n");
    origin.subscription =
        "proxy-providers:\n  managed:\n    type: http\n    url: https://127.0.0.1:" +
        QByteArray::number(origin.serverPort()) +
        "/provider_nodes\n    interval: 3600\nrule-providers:\n  domains:\n    type: http\n    "
        "behavior: classical\n    url: https://127.0.0.1:" +
        QByteArray::number(origin.serverPort()) +
        "/provider_rules\n    interval: 3600\nproxy-groups:\n  - {name: managed_group, type: "
        "select, use: [managed]}\nrules:\n  - RULE-SET,domains,managed_group\n  - MATCH,DIRECT\n";
    reopened.addSubscription(source, QStringLiteral("Managed providers"));
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("profiles").toArray().size() == 7,
                 "managed HTTPS provider snapshots", reopened))
        return 122;
    const auto providerId =
        reopened.snapshot().value("profiles").toArray().last().toObject().value("id").toString();
    reopened.selectProfile(providerId);
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     !reopened.snapshot().value("groups").toArray().isEmpty(),
                 "provider nodes offline", reopened))
        return 123;
    reopened.start(false);
    if (!require(waitFor([&] {
                     return reopened.snapshot().value("ready").toBool() &&
                            !reopened.snapshot().value("busy").toBool();
                 }),
                 "managed provider Ready", reopened))
        return 124;
    reopened.setDemands({QStringLiteral("rules")});
    reopened.featureCommand(QStringLiteral("rules"), {});
    if (!require(waitFor([&] { return reopened.snapshot().value("rules").toArray().size() >= 3; }),
                 "expanded RULE-SET installed", reopened))
        return 125;
    const auto providerHash =
        reopened.snapshot().value("profiles").toArray().last().toObject().value("snapshotHash");
    const auto providerPid = reopened.snapshot().value("corePid");
    const auto configIdle = [&] {
        const auto counters = reopened.snapshot().value("configLifecycle").toObject();
        return counters.value("providerFiles").toInteger() == 0 &&
               counters.value("providerBytes").toInteger() == 0 &&
               counters.value("semanticBytes").toInteger() == 0 &&
               !counters.value("validationActive").toBool();
    };
    const auto stagingEmpty = [&] {
        return QDir(store.root() + "/cache/config_stage")
            .entryList(QDir::Files | QDir::Hidden)
            .isEmpty();
    };
    reopened.updateProfile(providerId);
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("corePid") == providerPid && configIdle() &&
                     stagingEmpty(),
                 "unchanged provider snapshot does not restart and releases transaction buffers",
                 reopened))
        return 142;
    origin.payloads.insert("/provider_rules",
                           "payload: ['DOMAIN,changed.invalid', 'DOMAIN-SUFFIX,example.com']\n");
    reopened.updateProfile(providerId);
    if (!require(waitFor([&] {
                     return !reopened.snapshot().value("busy").toBool() &&
                            reopened.snapshot().value("ready").toBool();
                 }) &&
                     reopened.snapshot().value("corePid") != providerPid && configIdle() &&
                     stagingEmpty() &&
                     reopened.snapshot()
                             .value("profiles")
                             .toArray()
                             .last()
                             .toObject()
                             .value("snapshotHash") != providerHash,
                 "changed provider uses existing apply transaction and releases launch semantic",
                 reopened))
        return 143;
    const auto changedHash =
        reopened.snapshot().value("profiles").toArray().last().toObject().value("snapshotHash");
    const auto changedPid = reopened.snapshot().value("corePid");
    origin.payloads.insert("/provider_nodes", "invalid");
    reopened.updateProfile(providerId);
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("ready").toBool() &&
                     reopened.snapshot()
                             .value("profiles")
                             .toArray()
                             .last()
                             .toObject()
                             .value("snapshotHash") == changedHash &&
                     reopened.snapshot().value("corePid") == changedPid && configIdle() &&
                     stagingEmpty(),
                 "bad provider preserves running snapshot", reopened))
        return 126;
    origin.payloads.insert(
        "/provider_nodes",
        "proxies: [{name: managed_fixture, type: http, server: 127.0.0.1, port: 9}]\n");
    bool cancellationQueued = false;
    const auto cancelConnection =
        QObject::connect(&reopened, &quickclash::application::DesktopRuntime::changed, &reopened,
                         [&](const QJsonObject&) {
                             if (!cancellationQueued && reopened.snapshot()
                                                                .value("configLifecycle")
                                                                .toObject()
                                                                .value("semanticBytes")
                                                                .toInteger() > 0) {
                                 cancellationQueued = true;
                                 QTimer::singleShot(0, &reopened, [&] { reopened.stop(); });
                             }
                         });
    reopened.updateProfile(providerId);
    if (!require(waitFor([&] {
                     return cancellationQueued && !reopened.snapshot().value("busy").toBool();
                 }) &&
                     configIdle() && stagingEmpty() &&
                     reopened.snapshot()
                             .value("profiles")
                             .toArray()
                             .last()
                             .toObject()
                             .value("snapshotHash") == changedHash,
                 "stop during provider validation clears private transaction and preserves catalog",
                 reopened))
        return 144;
    QObject::disconnect(cancelConnection);
    QCoreApplication::processEvents();
    if (!require(configIdle() && !reopened.snapshot().value("ready").toBool() && stagingEmpty(),
                 "late validation callbacks cannot revive stopped transaction", reopened))
        return 145;
    reopened.stop();
    evidence.insert(QStringLiteral("managedProviderChecks"),
                    QJsonArray{"https_snapshots", "offline_nodes", "official_validation_ready",
                               "ruleset_expansion", "bad_update_retains_session",
                               "unchanged_no_restart", "changed_transaction_apply",
                               "stage_buffers_released", "stop_rejects_late_validation"});
    if (!require(!reopened.snapshot().value("groups").toArray().isEmpty(),
                 "offline nodes remain after stop", reopened))
        return 127;
    reopened.featureCommand(QStringLiteral("cancel_test"), {});
    if (!require(reopened.snapshot()
                         .value("features")
                         .toObject()
                         .value("websiteTest")
                         .toObject()
                         .value("state") == "Cancelled",
                 "cancel clears website state even between queued requests", reopened))
        return 128;
    // The retained subscription revision restores only after confirmation.
    for (const auto& entry : reopened.snapshot().value("profiles").toArray()) {
        const auto item = entry.toObject();
        if (item.value("history").toArray().isEmpty())
            continue;
        const auto previousHash =
            item.value("history").toArray().first().toObject().value("sourceHash");
        reopened.featureCommand(QStringLiteral("rollback_profile"),
                                {item.value("id").toString(), QStringLiteral("confirmed")});
        if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }),
                     "rollback completes", reopened))
            return 129;
        bool found = false;
        for (const auto& restored : reopened.snapshot().value("profiles").toArray())
            found |= restored.toObject().value("id") == item.value("id") &&
                     restored.toObject().value("sourceHash") == previousHash;
        if (!require(found, "previous snapshot restored", reopened))
            return 130;
        break;
    }
    {
        QTemporaryDir managedDirectory;
        quickclash::infra::JsonStore managedStore(managedDirectory.path());
        if (!std::holds_alternative<std::monostate>(managedStore.open()))
            return 131;
        MemoryProxySettings settings;
        const auto original = settings.value;
        quickclash::application::DesktopRuntime managed(managedStore, nullptr, &settings);
        managed.initialize();
        int managedMixed = port(), managedController = port();
        while (managedMixed == managedController)
            managedController = port();
        managed.setPorts(managedMixed, managedController);
        managed.enableBackgroundManagement();
        if (!require(!managed.snapshot().value("ready").toBool() &&
                         !managed.snapshot().value("recovering").toBool(),
                     "empty managed runtime waits for a profile", managed))
            return 132;
        const auto fixturePath = managedDirectory.filePath(QStringLiteral("nodes.yaml"));
        QFile managedFixture(fixturePath);
        if (!managedFixture.open(QIODevice::WriteOnly))
            return 133;
        managedFixture.write(
            "proxies:\n  - {name: first, type: http, server: 127.0.0.1, port: 9}\n"
            "  - {name: second, type: http, server: 127.0.0.1, port: 9}\n"
            "proxy-groups:\n  - {name: managed_group, type: select, proxies: [first, second]}\n"
            "rules: ['MATCH,managed_group']\n");
        managedFixture.close();
        managed.importFile(QUrl::fromLocalFile(fixturePath).toString(),
                           QStringLiteral("Managed A"));
        const auto isReady = [&] {
            return managed.snapshot().value("ready").toBool() &&
                   !managed.snapshot().value("busy").toBool();
        };
        if (!require(waitFor(isReady) && !managed.snapshot().value("systemProxy").toBool(),
                     "first import starts background core without enabling network switches",
                     managed))
            return 134;
        const auto firstId = managed.snapshot().value("selectedId").toString();
        const auto firstPid = managed.snapshot().value("corePid").toInt();
        (void)managed.setSystemProxy(true);
        (void)managed.setSystemProxy(false);
        if (!require(isReady() && managed.snapshot().value("corePid").toInt() == firstPid &&
                         settings.value == original,
                     "system proxy off preserves core and restores platform settings", managed))
            return 135;
        managed.featureCommand(QStringLiteral("delay_target"),
                               {QStringLiteral(" https://example.invalid/delay ")});
        if (!require(managed.snapshot().value("delayTarget") == "https://example.invalid/delay",
                     "node delay target is validated, trimmed and published from settings",
                     managed))
            return 141;
        managed.featureCommand(
            QStringLiteral("delay_batch"),
            {QStringLiteral("managed_group"), managed.snapshot().value("delayTarget").toString()});
        if (!require(waitFor([&] {
                         return managed.snapshot()
                                    .value("features")
                                    .toObject()
                                    .value("delayTest")
                                    .toObject()
                                    .value("state") == "Completed";
                     }) &&
                         managed.snapshot().value("delays").toObject().size() == 2,
                     "batch delay tests every node and completes despite unavailable nodes",
                     managed))
            return 136;
        managed.featureCommand(
            QStringLiteral("delay"),
            {QStringLiteral("first"), QStringLiteral("https://example.invalid/")});
        managed.featureCommand(QStringLiteral("cancel_delays"), {});
        managed.importFile(QUrl::fromLocalFile(QDir::current().absoluteFilePath(QStringLiteral(
                                                   "tests/fixtures/mvp/dns_supported.yaml")))
                               .toString(),
                           QStringLiteral("Managed B"));
        if (!require(waitFor([&] { return !managed.snapshot().value("busy").toBool(); }),
                     "second managed import", managed))
            return 137;
        const auto secondId =
            managed.snapshot().value("profiles").toArray().last().toObject().value("id").toString();
        managed.selectProfile(secondId);
        if (!require(waitFor(isReady) && managed.snapshot().value("selectedId") == secondId &&
                         managed.snapshot().value("delays").toObject().isEmpty(),
                     "card selection switches running profile and invalidates old delay results",
                     managed))
            return 138;
        managed.selectProfile(firstId);
        if (!require(waitFor(isReady), "switch back to managed subscription", managed))
            return 139;
        for (int crash = 0; crash < 4; ++crash) {
            const auto pid = managed.snapshot().value("corePid").toInt();
            if (!quickclash::test::terminateOwnedFixture(static_cast<quint32>(pid)) ||
                !require(waitFor([&] {
                             return isReady() && managed.snapshot().value("corePid").toInt() != pid;
                         }),
                         "managed core keeps recovering beyond three crashes", managed))
                return 140;
        }
        managed.stop(quickclash::domain::StopReason::AppExit);
        if (!require(!managed.snapshot().value("ready").toBool() &&
                         !managed.snapshot().value("recovering").toBool() &&
                         settings.value == original,
                     "exit cancels recovery and restores network", managed))
            return 141;
    }
    evidence.insert(QStringLiteral("managedCoreChecks"),
                    QJsonArray{"empty_waits", "first_import_autostarts", "proxy_off_keeps_core",
                               "batch_node_delays", "switch_invalidates_old_delays",
                               "four_crashes_recover", "exit_cleanup"});
    QSaveFile evidenceFile(QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("p2_runtime_evidence.json")));
    if (!evidenceFile.open(QIODevice::WriteOnly) ||
        evidenceFile.write(QJsonDocument(evidence).toJson()) < 0 || !evidenceFile.commit())
        return 23;
    std::cout << "Import, isolated -t, stopped selection, owned Ready, encoded selection readback, "
                 "mode, stop and restart passed.\n";
    return 0;
}
