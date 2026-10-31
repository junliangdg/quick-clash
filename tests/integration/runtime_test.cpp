#include "application/desktop_runtime.h"
#ifdef Q_OS_WIN
#include "platform/windows/system_proxy.h"
#endif
#include "backup_probe.h"
#include "config_editor_probe.h"
#include "geo_update_probe.h"
#include "native_proxy_client.h"
#include "network_fixture.h"
#include "provider_probe.h"
#include "subscription_download_probe.h"
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
#include <yaml-cpp/yaml.h>

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
    if (app.arguments().contains(QStringLiteral("--provider-test")))
        return quickclash::test::providerProbe();
    if (app.arguments().contains(QStringLiteral("--backup-test")))
        return quickclash::test::backupProbe();
    if (app.arguments().contains(QStringLiteral("--config-editor-test")))
        return quickclash::test::configEditorProbe();
    if (app.arguments().contains(QStringLiteral("--geo-update-test")))
        return quickclash::test::geoUpdateProbe();
    if (app.arguments().contains(QStringLiteral("--subscription-download-test")))
        return quickclash::test::subscriptionDownloadProbe();
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
    const auto settingsBeforeSuggestion = runtime.snapshot().value("preferences");
    runtime.featureCommand(QStringLiteral("suggest_listener_ports"), {});
    if (!require(runtime.snapshot().value("preferences") == settingsBeforeSuggestion &&
                     runtime.snapshot()
                             .value("features")
                             .toObject()
                             .value("listenerPortCandidate")
                             .toObject()
                             .value("mixedPort")
                             .toInt() == mixed,
                 "port suggestion is a preview without saved changes", runtime))
        return 180;
    runtime.featureCommand("cancel_listener_ports", {});
    if (!require(
            !runtime.snapshot().value("features").toObject().contains("listenerPortCandidate") &&
                runtime.snapshot().value("preferences") == settingsBeforeSuggestion,
            "cancelling ports consumes the preview without saved changes", runtime))
        return 183;
    if (!require(runtime.snapshot()
                             .value("settingsState")
                             .toObject()
                             .value("saved")
                             .toObject()
                             .value("mixedPort")
                             .toInt() == mixed &&
                     !runtime.snapshot()
                          .value("settingsState")
                          .toObject()
                          .value("effective")
                          .toObject()
                          .contains("mixedPort"),
                 "stopped preferences are saved without becoming effective", runtime))
        return 157;
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
    runtime.featureCommand("network_settings",
                           {"global", QString::number(mixed), QString::number(controller),
                            "profile", runtime.snapshot().value("epoch").toString(), "0"});
    if (!require(runtime.snapshot().value("preferences").toObject().value("mode") == "rule",
                 "network confirmation rejects changed selection", runtime))
        return 158;
    runtime.featureCommand("network_settings",
                           {"global", QString::number(mixed), QString::number(controller),
                            "profile", runtime.snapshot().value("epoch").toString(),
                            runtime.snapshot().value("selectionGeneration").toString()});
    const auto requestedSettings = runtime.snapshot().value("settingsState").toObject();
    if (!require(requestedSettings.value("pending").toBool() &&
                     requestedSettings.value("requested").toObject().value("mode") == "global" &&
                     requestedSettings.value("effective").toObject().value("mode") == "rule" &&
                     requestedSettings.value("saved").toObject().value("mode") == "rule",
                 "requested change retains old effective settings until confirmation", runtime))
        return 159;
    if (!require(waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }) &&
                     runtime.snapshot().value("ready").toBool() &&
                     runtime.snapshot().value("preferences").toObject().value("mode") ==
                         QStringLiteral("global"),
                 "mode", runtime))
        return 7;
    const auto unchangedSettings = runtime.snapshot().value("preferences").toObject();
    runtime.featureCommand("suggest_listener_ports", {});
    runtime.featureCommand("network_settings", {unchangedSettings.value("mode").toString(),
                                                QString::number(mixed), QString::number(controller),
                                                unchangedSettings.value("dnsMode").toString(),
                                                runtime.snapshot().value("epoch").toString(),
                                                QString::number(runtime.selectionGeneration())});
    if (!require(
            !runtime.snapshot().value("features").toObject().contains("listenerPortCandidate") &&
                runtime.snapshot().value("preferences") == unchangedSettings,
            "confirming unchanged ports consumes the preview", runtime))
        return 184;
    runtime.stop();
    if (!require(!runtime.snapshot().value("ready").toBool() &&
                     !runtime.snapshot().value("groups").toArray().isEmpty() &&
                     !runtime.snapshot()
                          .value("settingsState")
                          .toObject()
                          .value("effective")
                          .toObject()
                          .contains("mixedPort"),
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
    origin.subscriptionUsage = "upload=10;download=80;total=100;expire=1800086400";
    reopened.addSubscription(QStringLiteral("  ") + source + QStringLiteral("\r\n"),
                             QStringLiteral("Fixture B HTTPS"));
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("profiles").toArray().size() == 2 &&
                     origin.lastUserAgent.startsWith("clash.meta/1.19.33") &&
                     origin.lastAccept.contains("application/yaml"),
                 "https subscription", reopened))
        return 13;
    auto second = reopened.snapshot().value("profiles").toArray()[1].toObject();
    {
        quickclash::application::DesktopRuntime metadataReopened(store, nullptr, nullptr,
                                                                 [&] { return subscriptionTime; });
        metadataReopened.initialize();
        const auto restored = metadataReopened.snapshot().value("profiles").toArray()[1].toObject();
        if (!require(restored.value("usage") == second.value("usage") &&
                         restored.value("usageCollectedAt") == second.value("usageCollectedAt") &&
                         restored.value("usageStatus") == second.value("usageStatus"),
                     "usage metadata and projected warnings survive reopen", metadataReopened))
            return 149;
    }
    if (!require(!second.value("usageCollectedAt").toString().isEmpty() &&
                     second.value("usageStatus").toObject().value("quotaState") == "Low",
                 "subscription metadata records collection time and warning", reopened))
        return 147;
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
    const auto websiteState = [&] {
        return reopened.snapshot().value("features").toObject().value("websiteTest").toObject();
    };
    const auto probeWebsite = [&](const QString& suffix, const QString& expected,
                                  const QString& route = QStringLiteral("CurrentProxy")) {
        reopened.testWebsite(
            QStringLiteral("https://%1:%2%3")
                .arg(route == "CurrentProxy" ? "fixture.quickclash.invalid" : "127.0.0.1")
                .arg(origin.serverPort())
                .arg(suffix),
            route);
        return waitFor([&] { return websiteState().value("state") != "Running"; }) &&
               websiteState().value("state") == expected;
    };
    origin.redirects.insert("/redirect", "/redirect-final");
    origin.redirects.insert("/loop", "/loop");
    origin.redirects.insert("/downgrade", "http://127.0.0.1/never-requested");
    origin.redirects.insert("/credentials", "https://user:secret@127.0.0.1/never-requested");
    origin.redirects.insert(
        "/cross-origin",
        QStringLiteral("https://127.0.0.1:%1/direct-final").arg(origin.serverPort()).toUtf8());
    for (int hop = 0; hop < 4; ++hop)
        origin.redirects.insert("/hop" + QByteArray::number(hop),
                                "/hop" + QByteArray::number(hop + 1));
    origin.payloads.insert("/large", QByteArray(65537, 'x'));
    origin.payloads.insert("/chain-limit", QByteArray(40000, 'x'));
    origin.payloads.insert("/chain-final", QByteArray(40000, 'x'));
    origin.redirects.insert("/chain-limit", "/chain-final");
    if (!require(
            probeWebsite("/redirect", "Reachable") &&
                websiteState().value("redirects").toInt() == 1 && origin.lastCookie.isEmpty() &&
                websiteState().value("mode") == "rule" &&
                !websiteState().value("finalOrigin").toString().isEmpty() &&
                probeWebsite("/loop", "RedirectLimit") && probeWebsite("/hop0", "RedirectLimit") &&
                websiteState().value("redirects").toInt() == 3 &&
                probeWebsite("/downgrade", "RedirectBlocked") &&
                probeWebsite("/credentials", "RedirectBlocked") &&
                probeWebsite("/large", "ResponseLimit") &&
                websiteState().value("bytes").toInt() == 65537 &&
                probeWebsite("/chain-limit", "ResponseLimit") &&
                probeWebsite("/cross-origin", "Reachable", "Direct") && origin.lastCookie.isEmpty(),
            "bounded HTTPS redirects, aggregate bytes and no cookies", reopened))
        return 155;
    const auto remotePort = remote.serverPort();
    const auto originBeforeFailure = origin.requests.size();
    remote.close();
    reopened.testWebsite(QStringLiteral("https://fixture.quickclash.invalid:%1/no-fallback")
                             .arg(origin.serverPort()));
    if (!require(waitFor([&] { return websiteState().value("state") != "Running"; }) &&
                     websiteState().value("state") != "Reachable" &&
                     origin.requests.size() == originBeforeFailure &&
                     remote.listen(QHostAddress::LocalHost, remotePort),
                 "unavailable upstream never falls back to direct origin", reopened))
        return 162;
    reopened.testWebsite(QStringLiteral("https://no-such-host.quickclash.invalid/"),
                         QStringLiteral("Direct"));
    if (!require(waitFor([&] { return websiteState().value("state") != "Running"; }) &&
                     (websiteState().value("state") == "DnsError" ||
                      websiteState().value("state") == "NetworkError"),
                 "unreachable hostname remains a DNS or network failure under system DNS",
                 reopened))
        return 163;
    reopened.testWebsite(
        QStringLiteral("https://localhost:%1/tls-mismatch").arg(origin.serverPort()),
        QStringLiteral("Direct"));
    if (!require(waitFor([&] { return websiteState().value("state") != "Running"; }) &&
                     websiteState().value("state") == "TlsError",
                 "TLS hostname mismatch is not bypassed", reopened))
        return 156;
    origin.responseDelays.insert("/slow-first", 3000);
    origin.responseDelays.insert("/slow-final", 11000);
    origin.redirects.insert("/slow-first", "/slow-final");
    if (!require(probeWebsite("/slow-first", "Timeout") &&
                     websiteState().value("elapsedMs").toInt() <= 12500,
                 "redirect chain shares one overall deadline", reopened))
        return 157;
    const auto countBeforeCancel = origin.requests.size();
    reopened.featureCommand(
        QStringLiteral("test_batch"),
        {QStringLiteral("Direct"),
         QStringLiteral("https://127.0.0.1:%1/slow-final").arg(origin.serverPort()),
         QStringLiteral("https://127.0.0.1:%1/never-queued").arg(origin.serverPort())});
    if (!waitFor([&] { return origin.requests.size() > countBeforeCancel; }))
        return 158;
    reopened.featureCommand(QStringLiteral("cancel_test"), {});
    QCoreApplication::processEvents();
    if (!require(websiteState().value("state") == "Cancelled" &&
                     !origin.requests.contains(QStringLiteral("GET /never-queued")),
                 "batch cancellation releases queue and rejects late reply", reopened))
        return 159;
    if (!probeWebsite("/before-stale", "Reachable"))
        return 160;
    reopened.reconcile();
    if (!require(websiteState().value("state") == "Stale" && reopened.snapshot()
                                                                 .value("features")
                                                                 .toObject()
                                                                 .value("testHistory")
                                                                 .toArray()
                                                                 .last()
                                                                 .toObject()
                                                                 .value("stale")
                                                                 .toBool(),
                 "network changes invalidate completed results and history immediately", reopened))
        return 161;
    for (int batch = 0; batch < 4; ++batch) {
        QStringList urls{QStringLiteral("Direct")};
        for (int item = 0; item < 8; ++item)
            urls.append(QStringLiteral("https://127.0.0.1:%1/batch-%2-%3")
                            .arg(origin.serverPort())
                            .arg(batch)
                            .arg(item));
        reopened.featureCommand(QStringLiteral("test_batch"), urls);
        const auto last = urls.last();
        if (!require(waitFor([&] {
                         return websiteState().value("state") == "Reachable" &&
                                websiteState().value("target") == last;
                     }) &&
                         reopened.snapshot()
                                 .value("features")
                                 .toObject()
                                 .value("testHistory")
                                 .toArray()
                                 .size() <= 32,
                     "finite serial batches keep history bounded", reopened))
            return 164;
    }
    if (!require(reopened.snapshot()
                         .value("features")
                         .toObject()
                         .value("testHistory")
                         .toArray()
                         .size() == 32,
                 "history evicts oldest results", reopened))
        return 165;
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
    origin.subscriptionUsage = "download=95;total=100";
    reopened.updateProfile(subscriptionId);
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("corePid") == unchangedPid,
                 "unchanged update does not restart core", reopened))
        return 63;
    if (!require(reopened.snapshot()
                         .value("profiles")
                         .toArray()[1]
                         .toObject()
                         .value("usageStatus")
                         .toObject()
                         .value("quotaState") == "Unknown",
                 "same-body update replaces missing metadata without guessing zero upload",
                 reopened))
        return 148;
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
    reopened.featureCommand("network_settings",
                            {"rule", QString::number(occupied.serverPort()),
                             QString::number(controller), "profile",
                             reopened.snapshot().value("epoch").toString(),
                             reopened.snapshot().value("selectionGeneration").toString()});
    if (!require(
            waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }, 20000) &&
                reopened.snapshot().value("ready").toBool() &&
                reopened.snapshot().value("preferences").toObject().value("mixedPort").toInt() ==
                    mixed &&
                reopened.snapshot()
                        .value("settingsState")
                        .toObject()
                        .value("effective")
                        .toObject()
                        .value("mixedPort")
                        .toInt() == mixed &&
                !reopened.snapshot().value("settingsState").toObject().value("pending").toBool(),
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
    quickclash::test::DnsFixture dnsOrigin;
    if (!dnsOrigin.initialize())
        return 166;
    QFile dnsInput(QStringLiteral("tests/fixtures/mvp/dns_supported.yaml"));
    if (!dnsInput.open(QIODevice::ReadOnly))
        return 167;
    auto dnsBytes = dnsInput.readAll();
    dnsBytes.replace("https://dns.example.invalid/dns-query", dnsOrigin.resolver.toUtf8());
    dnsBytes.replace("tls://1.0.0.1:853", dnsOrigin.resolver.toUtf8());
    dnsBytes += "\n  cache-algorithm: lru\n  nameserver-policy: {'+.quickclash.test': ['" +
                dnsOrigin.resolver.toUtf8() + "']}\n  fake-ip-filter: ['+.quickclash.test']\n";
    const auto dnsPath = directory.filePath(QStringLiteral("controlled_dns.yaml"));
    {
        QSaveFile dnsFile(dnsPath);
        if (!dnsFile.open(QIODevice::WriteOnly) || dnsFile.write(dnsBytes) != dnsBytes.size() ||
            !dnsFile.commit())
            return 168;
    }
    reopened.importFile(QUrl::fromLocalFile(dnsPath).toString(),
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
    const auto queryDns = [&](const QString& name) {
        const auto epoch = reopened.snapshot().value("epoch").toString();
        QFile session(
            directory.filePath(QStringLiteral("data/sessions/%1/config.yaml").arg(epoch)));
        if (!session.open(QIODevice::ReadOnly))
            return QByteArray{};
        const auto config = YAML::Load(session.readAll().toStdString());
        QNetworkAccessManager api;
        api.setProxy(QNetworkProxy::NoProxy);
        QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/dns/query?name=%2&type=A")
                                         .arg(controller)
                                         .arg(name)));
        request.setRawHeader("Authorization", "Bearer " + QByteArray::fromStdString(
                                                              config["secret"].as<std::string>()));
        request.setTransferTimeout(3000);
        auto* reply = api.get(request);
        return waitFor([&] { return reply->isFinished(); }, 4000) &&
                       reply->error() == QNetworkReply::NoError
                   ? reply->readAll()
                   : QByteArray{};
    };
    if (!require(queryDns("fixture.quickclash.test").contains("203.0.113.7") &&
                     dnsOrigin.requests > 0,
                 "official fake-ip filtered policy uses controlled DNS", reopened))
        return 169;
    const auto cachedDnsRequests = dnsOrigin.requests;
    if (!require(queryDns("fixture.quickclash.test").contains("203.0.113.7") &&
                     dnsOrigin.requests == cachedDnsRequests,
                 "DNS response cache reused", reopened))
        return 170;
    reopened.featureCommand(QStringLiteral("flush_dns"), {});
    if (!require(waitFor([&] {
                     return reopened.snapshot().value("features").toObject().value("dnsResult") ==
                            "Cleared";
                 }) &&
                     queryDns("fixture.quickclash.test").contains("203.0.113.7") &&
                     dnsOrigin.requests > cachedDnsRequests,
                 "DNS cache flush forces controlled requery", reopened))
        return 171;
    reopened.featureCommand(QStringLiteral("dns_mode"), {QStringLiteral("redir-host")});
    if (!require(waitFor([&] {
                     return !reopened.snapshot().value("busy").toBool() &&
                            reopened.snapshot().value("ready").toBool();
                 }) &&
                     queryDns("fixture.quickclash.test").contains("203.0.113.7"),
                 "redir-host override retains policy and controlled resolution", reopened))
        return 172;
    reopened.featureCommand(QStringLiteral("dns_mode"), {QStringLiteral("profile")});
    if (!require(waitFor([&] {
                     return !reopened.snapshot().value("busy").toBool() &&
                            reopened.snapshot().value("ready").toBool();
                 }),
                 "profile DNS override restoration", reopened))
        return 173;
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
    int balancedCount = 0;
    for (const auto& entry : reopened.snapshot().value("groups").toArray()) {
        const auto group = entry.toObject();
        if (group.value("type") == "LoadBalance") {
            if (!require(!group.value("canSelect").toBool() &&
                             group.value("current").toString().isEmpty(),
                         "official load balancing capability has no single current member",
                         reopened))
                return 174;
            ++balancedCount;
        }
    }
    if (!require(balancedCount == 3, "all load balancing strategies loaded", reopened))
        return 175;
    for (const auto& entry :
         reopened.snapshot().value("features").toObject().value("nodes").toArray())
        if (!require(entry.toObject().value("type") == "tuic" &&
                         entry.toObject().value("udp").toBool(),
                     "TUIC native UDP capability projection", reopened))
            return 176;
    const auto advancedPid = reopened.snapshot().value("corePid");
    reopened.selectProxy(QStringLiteral("Balanced"), QStringLiteral("DIRECT"));
    if (!require(reopened.snapshot().value("error") == "ProxyGroupAutomatic" &&
                     reopened.snapshot().value("corePid") == advancedPid &&
                     !reopened.snapshot().value("busy").toBool(),
                 "load balancing rejects manual mutation without restart", reopened))
        return 177;
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
    if (!require(
            waitFor([&] {
                const auto result =
                    reopened.snapshot().value("features").toObject().value("dnsResult").toString();
                return result == "Cleared" || result == "Failed";
            }) &&
                reopened.snapshot().value("features").toObject().value("dnsResult") == "Cleared",
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
    const auto resourceMemory =
        reopened.snapshot().value("features").toObject().value("memory").toObject();
    if (!require(resourceMemory.value("core").toObject().value("pid").toString() ==
                         reopened.snapshot().value("corePid").toVariant().toString() &&
                     resourceMemory.value("host")
                             .toObject()
                             .value("privateCommit")
                             .toString()
                             .toULongLong() > 0,
                 "resource sampler verifies owned core identity and host counters", reopened))
        return 159;
    QTcpSocket capturedConnection;
    const auto holdConnection = [&](QTcpSocket& socket) {
        socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(mixed));
        if (!waitFor([&] { return socket.state() == QAbstractSocket::ConnectedState; }))
            return false;
        socket.write("CONNECT 127.0.0.1:" + QByteArray::number(origin.serverPort()) +
                     " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        return waitFor([&] { return socket.bytesAvailable() > 0; }) &&
               socket.readAll().contains("200");
    };
    if (!require(holdConnection(capturedConnection), "open held connection", reopened))
        return 150;
    QString capturedId;
    if (!require(waitFor([&] {
                     for (const auto& value : reopened.snapshot().value("connections").toArray()) {
                         const auto entry = value.toObject();
                         if (entry.value("metadata")
                                 .toObject()
                                 .value("sourcePort")
                                 .toVariant()
                                 .toInt() == capturedConnection.localPort())
                             capturedId = entry.value("id").toString();
                     }
                     return !capturedId.isEmpty();
                 }),
                 "held connection snapshot", reopened))
        return 151;
    reopened.featureCommand("close_connection", {"old-epoch", capturedId});
    if (!require(!reopened.snapshot().value("features").toObject().value("closeRunning").toBool(),
                 "old epoch rejected", reopened))
        return 152;
    QTcpSocket laterConnection;
    if (!require(holdConnection(laterConnection), "open later connection", reopened))
        return 153;
    reopened.featureCommand("close_filtered_connections",
                            {reopened.snapshot().value("epoch").toString(), "confirmed", capturedId,
                             "00000000-0000-4000-8000-000000000099"});
    if (!require(
            waitFor([&] {
                const auto state = reopened.snapshot().value("features").toObject();
                return !state.value("closeRunning").toBool() &&
                       state.value("closeRemaining").toInt() == 0;
            }) &&
                reopened.snapshot().value("features").toObject().value("closeSucceeded").toInt() ==
                    1 &&
                reopened.snapshot().value("features").toObject().value("closeEnded").toInt() == 1 &&
                laterConnection.state() == QAbstractSocket::ConnectedState,
            "filtered close accounts ended target and preserves new connections", reopened))
        return 154;
    reopened.featureCommand("close_all_connections",
                            {reopened.snapshot().value("epoch").toString(), "confirmed"});
    if (!require(
            waitFor([&] {
                return reopened.snapshot().value("features").toObject().value("closeAllResult") ==
                       "Completed";
            }),
            "close all reports result", reopened))
        return 155;
    reopened.setDemands({});
    reopened.stop();
    if (!require(!reopened.snapshot().value("features").toObject().contains("closeAllResult"),
                 "stop clears connection close results", reopened))
        return 156;
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
    if (!require(reopened.snapshot().value("error") == "BackupPreviewRequired",
                 "restore requires reviewed preview", reopened))
        return 157;
    reopened.featureCommand(QStringLiteral("preview_backup"), {archiveUrl});
    const auto restoreToken = reopened.snapshot()
                                  .value("features")
                                  .toObject()
                                  .value("backupPreview")
                                  .toObject()
                                  .value("token")
                                  .toString();
    reopened.featureCommand(QStringLiteral("restore_backup"),
                            {restoreToken, QStringLiteral("confirmed")});
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
    reopened.featureCommand(QStringLiteral("preview_backup"),
                            {QUrl::fromLocalFile(badArchivePath).toString()});
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
    QTimer cancelValidation;
    cancelValidation.setInterval(0);
    // Diagnostic lifecycle counters can change without a compact control-state
    // notification. Observe them independently while validation is in flight.
    const auto cancelConnection =
        QObject::connect(&cancelValidation, &QTimer::timeout, &reopened, [&] {
            const auto lifecycle = reopened.snapshot().value("configLifecycle").toObject();
            if (!cancellationQueued && lifecycle.value("validationActive").toBool() &&
                lifecycle.value("semanticBytes").toInteger() > 0) {
                cancellationQueued = true;
                cancelValidation.stop();
                reopened.stop();
            }
        });
    cancelValidation.start();
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
    reopened.featureCommand(QStringLiteral("rollback_profile"),
                            {providerId, QStringLiteral("confirmed"), QStringLiteral("0")});
    if (!require(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                     reopened.snapshot().value("error").toString().isEmpty() &&
                     reopened.snapshot()
                             .value("profiles")
                             .toArray()
                             .last()
                             .toObject()
                             .value("snapshotHash") == providerHash,
                 "historical provider snapshot reuses verified immutable content", reopened))
        return 146;
    reopened.stop();
    evidence.insert(QStringLiteral("managedProviderChecks"),
                    QJsonArray{"https_snapshots", "offline_nodes", "official_validation_ready",
                               "ruleset_expansion", "bad_update_retains_session",
                               "unchanged_no_restart", "changed_transaction_apply",
                               "stage_buffers_released", "stop_rejects_late_validation"});
    if (!require(!reopened.snapshot().value("groups").toArray().isEmpty(),
                 "offline nodes remain after stop", reopened))
        return 127;
    reopened.testWebsite(QStringLiteral("https://127.0.0.1:%1/slow-final").arg(origin.serverPort()),
                         QStringLiteral("Direct"));
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
        auto managedNow = QDateTime::currentDateTimeUtc();
        quickclash::application::DesktopRuntime managed(managedStore, nullptr, &settings,
                                                        [&] { return managedNow; });
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
        managedFixture.write("proxies:\n  - {name: first, type: http, server: 127.0.0.1, port: 9}\n"
                             "  - {name: second, type: http, server: 127.0.0.1, port: 9}\n"
                             "  - {name: third, type: http, server: 127.0.0.1, port: 9}\n"
                             "proxy-groups:\n  - {name: managed_group, type: select, proxies: "
                             "[first, second, third]}\n"
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
        managed.selectProxy(QStringLiteral("managed_group"), QStringLiteral("second"));
        managed.selectProxy(QStringLiteral("managed_group"), QStringLiteral("third"));
        managed.selectProxy(QStringLiteral("managed_group"), QStringLiteral("first"));
        if (!require(waitFor([&] {
                         for (const auto& value : managed.snapshot().value("groups").toArray()) {
                             const auto group = value.toObject();
                             if (group.value("name") == "managed_group")
                                 return isReady() && group.value("current") == "first";
                         }
                         return false;
                     }),
                     "rapid selection keeps the last intent and confirms core state", managed))
            return 142;
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
            {QStringLiteral("managed_group"), QStringLiteral("https://ignored.invalid/")});
        const auto queued = managed.snapshot().value("delays").toObject();
        if (!require(queued.value("first") == "Testing" && queued.value("second") == "Testing" &&
                         queued.value("third") == "Queued" &&
                         managed.snapshot().value("delayTarget") == "https://example.invalid/delay",
                     "delay concurrency is two and command cannot override settings target",
                     managed))
            return 143;
        managed.featureCommand(QStringLiteral("cancel_delays"), {});
        if (!require(managed.snapshot().value("delays").toObject().value("third") == "Cancelled",
                     "cancel also marks nodes still in the queue", managed))
            return 144;
        QThread::msleep(30);
        QCoreApplication::processEvents();
        if (!require(managed.snapshot().value("delays").toObject().value("first") == "Cancelled",
                     "late delay response cannot overwrite cancellation", managed))
            return 145;
        managed.featureCommand(QStringLiteral("delay_batch"), {QStringLiteral("managed_group")});
        if (!require(waitFor([&] {
                         return managed.snapshot()
                                    .value("features")
                                    .toObject()
                                    .value("delayTest")
                                    .toObject()
                                    .value("state") == "Completed";
                     }) &&
                         managed.snapshot().value("delays").toObject().size() == 3,
                     "batch delay tests every node and completes despite unavailable nodes",
                     managed))
            return 136;
        managedNow = managedNow.addSecs(601);
        managed.refresh();
        // A visible proxy demand shares the existing feature timer; no new cache timer.
        quickclash::application::RuntimeDemands delayDemand;
        delayDemand.active.insert(QStringLiteral("proxies"));
        delayDemand.generations.insert(QStringLiteral("proxies"), 1);
        managed.applyDemands(delayDemand);
        if (!require(managed.snapshot().value("delays").toObject().value("first") == "Stale",
                     "session delay cache expires after ten minutes", managed))
            return 146;
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
