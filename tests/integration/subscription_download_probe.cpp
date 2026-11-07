#include "subscription_download_probe.h"
#include "application/desktop_runtime.h"
#include "network_fixture.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace quickclash::test {
namespace {
bool waitFor(const std::function<bool()>& condition, int limit = 25000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < limit) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return condition();
}
int port() {
    QTcpServer listener;
    return listener.listen(QHostAddress::LocalHost) ? listener.serverPort() : 0;
}
} // namespace
int subscriptionDownloadProbe() {
    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (std::holds_alternative<domain::Error>(store.open()))
        return 1;
    auto now = QDateTime::currentDateTimeUtc();
    application::DesktopRuntime runtime(store, nullptr, nullptr, [&now] { return now; });
    runtime.initialize();
    const auto check = [&](bool result, const char* message) {
        if (!result)
            std::fprintf(stderr, "%s: %s\n", message,
                         QJsonDocument(runtime.snapshot()).toJson().constData());
        return result;
    };
    const auto done = [&] {
        return waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
    };
    const auto error = [&] { return runtime.snapshot().value("error").toString(); };
    const auto profile = [&] {
        const auto profiles = runtime.snapshot().value("profiles").toArray();
        return profiles.isEmpty() ? QJsonObject{} : profiles.first().toObject();
    };
    TlsFixture origin, other;
    if (!origin.initialize() || !other.initialize())
        return 2;
    ConnectFixture remote(origin.serverPort());
    if (!remote.listen(QHostAddress::LocalHost))
        return 3;
    auto mixed = port(), controller = port();
    while (mixed == controller)
        controller = port();
    runtime.setPorts(mixed, controller);
    origin.subscription = "proxies:\n- {name: fixture, type: http, x-vendor-node-id: public_dummy, "
                          "server: 127.0.0.1, port: " +
                          QByteArray::number(remote.serverPort()) +
                          "}\nproxy-groups:\n- {name: upstream, type: select, proxies: [fixture, "
                          "DIRECT]}\nrules:\n- MATCH,upstream\n"
                          "geox-url: {geoip: 'https://example.invalid/public-geo'}\n";
    other.payloads.insert("/final", origin.subscription);
    const auto source =
        QStringLiteral("https://127.0.0.1:%1/subscription").arg(origin.serverPort());
    const auto viaProxy = QStringLiteral("https://fixture.quickclash.invalid:%1/subscription")
                              .arg(origin.serverPort());
    origin.entityTags.insert("/subscription", "\"public-v1\"");
    origin.lastModified.insert("/subscription", "Wed, 28 Oct 2026 01:00:00 GMT");
    runtime.addSubscription(source, "Cache fixture", "RunningProxy");
    if (!check(!runtime.snapshot().value("busy").toBool() &&
                   error() == "SubscriptionProxyUnavailable" &&
                   runtime.snapshot().value("profiles").toArray().isEmpty(),
               "proxy requires ready core"))
        return 4;
    origin.extraHeaders.insert("/subscription",
                               "profile-title: base64:5py65Zy6\r\n"
                               "Content-Disposition: attachment; filename=other.yaml\r\n");
    origin.subscriptionUsage = "upload=10;download=20;total=100";
    runtime.addSubscription(source, "  ");
    if (!check(done() && error().isEmpty() && !profile().isEmpty() &&
                   origin.lastUserAgent.startsWith("clash.meta/1.19.33"),
               "first direct import uses the Clash Meta default"))
        return 5;
    const auto id = profile().value("id").toString();
    const auto notices = profile().value("compatibilityNotices").toArray();
    const auto savedRaw = store.readBytes(profile().value("sourceRef").toString());
    if (!check(notices.contains("ManagedGeo") && notices.contains("IgnoredProxyExtensions") &&
                   std::holds_alternative<QByteArray>(savedRaw) &&
                   std::get<QByteArray>(savedRaw) == origin.subscription &&
                   !QJsonDocument(runtime.snapshot()).toJson().contains("public-geo"),
               "compatibility notices persist without exposing or modifying source"))
        return 37;
    if (!check(profile().value("name") == QString::fromUtf8("机场"),
               "server title used for blank name"))
        return 34;
    QString returnedUrl, returnedId, returnedToken;
    QObject::connect(&runtime, &application::DesktopRuntime::subscriptionUrlReady, &runtime,
                     [&](const QString& item, const QString& token, const QString& url) {
                         returnedId = item;
                         returnedToken = token;
                         returnedUrl = url;
                     });
    runtime.requestSubscriptionUrl(id, "explicit-request");
    if (!check(returnedUrl == source && returnedId == id && returnedToken == "explicit-request" &&
                   !QJsonDocument(runtime.snapshot()).toJson().contains(source.toUtf8()),
               "explicit URL read is separate from published state"))
        return 35;
    runtime.requestSubscriptionUrl("missing", "missing-request");
    if (!check(returnedUrl.isEmpty(), "missing profile never returns a URL"))
        return 36;
    runtime.renameProfile(id, "Cache fixture");
    origin.extraHeaders.insert("/subscription", "profile-title: Changed by provider\r\n");
    runtime.configureSubscription(id, 0, false);
    const auto initialBody = origin.subscription;
    origin.subscription += "# manual update fixture\n";
    origin.entityTags.remove("/subscription");
    origin.lastModified.remove("/subscription");
    runtime.updateProfile(id);
    if (!check(done() && profile().value("pendingApply").toBool(),
               "scheduled policy leaves a selected update pending when automatic apply is off"))
        return 42;
    runtime.updateProfile(id, true);
    if (!check(done() && error().isEmpty() && !profile().value("pendingApply").toBool() &&
                   !runtime.snapshot().value("ready").toBool(),
               "manual update applies an unchanged pending candidate without starting the core"))
        return 43;
    origin.subscription += "# manual changed fixture\n";
    runtime.updateProfile(id, true);
    if (!check(done() && error().isEmpty() && !profile().value("pendingApply").toBool(),
               "manual changed update applies even when automatic apply is off"))
        return 44;
    QString sourceResultToken;
    QString sourceResultError;
    bool sourceResultSaved = false;
    QObject::connect(
        &runtime, &application::DesktopRuntime::subscriptionSourceFinished, &runtime,
        [&](const QString&, const QString& token, bool saved, const QString& errorCode) {
            sourceResultToken = token;
            sourceResultSaved = saved;
            sourceResultError = errorCode;
        });
    const auto priorSecret = profile().value("secretRef");
    origin.payloads.insert("/failed-edit", "<html>bad subscription</html>");
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/failed-edit", {}, true,
                                "failed-edit");
    if (!check(done() && sourceResultToken == "failed-edit" && !sourceResultSaved &&
                   !sourceResultError.isEmpty() && profile().value("secretRef") == priorSecret,
               "failed source edit reports its token and preserves the saved address"))
        return 45;
    origin.subscription = initialBody;
    runtime.changeProfileSource(id, source, {}, true, "saved-edit");
    if (!check(done() && sourceResultToken == "saved-edit" && sourceResultSaved &&
                   sourceResultError.isEmpty() && !profile().value("pendingApply").toBool(),
               "source edit saves and applies the selected stopped subscription"))
        return 46;
    const auto selected = runtime.snapshot().value("selectedId");
    runtime.addSubscription(source, "Other subscription");
    if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 2 &&
                   runtime.snapshot().value("selectedId") == selected,
               "adding another subscription retains the current selection"))
        return 48;
    const auto otherId =
        runtime.snapshot().value("profiles").toArray()[1].toObject().value("id").toString();
    origin.subscription += "# nonselected update\n";
    runtime.updateProfile(otherId, true);
    if (!check(done() && error().isEmpty() && runtime.snapshot().value("selectedId") == selected &&
                   !runtime.snapshot().value("ready").toBool() &&
                   runtime.snapshot()
                       .value("profiles")
                       .toArray()[1]
                       .toObject()
                       .value("pendingApply")
                       .toBool(),
               "manual update of another subscription does not select or start it"))
        return 49;
    runtime.deleteProfile(otherId);
    origin.subscription = initialBody;
    origin.entityTags.insert("/subscription", "\"public-v1\"");
    origin.lastModified.insert("/subscription", "Wed, 28 Oct 2026 01:00:00 GMT");
    runtime.updateProfile(id);
    if (!done() || !error().isEmpty())
        return 47;
    const auto originalHash = profile().value("sourceHash");
    const auto collectedAtImport = profile().value("usageCollectedAt");
    const auto usageAtImport = profile().value("usage");
    now = now.addSecs(120);
    origin.subscriptionUsage.clear();
    runtime.updateProfile(id);
    if (!check(done() && error().isEmpty() && profile().value("name") == "Cache fixture" &&
                   profile().value("sourceHash") == originalHash &&
                   profile().value("updatedAt") == now.toString(Qt::ISODate) &&
                   !collectedAtImport.toString().isEmpty() &&
                   profile().value("usageCollectedAt") == collectedAtImport &&
                   profile().value("usage") == usageAtImport &&
                   origin.requestHeaders.last().toObject().value("if-none-match") ==
                       "\"public-v1\"" &&
                   origin.requestHeaders.last().toObject().contains("if-modified-since") &&
                   origin.lastCookie.isEmpty(),
               "304 without usage keeps the previous sample and advances the update time"))
        return 6;
    now = now.addSecs(120);
    origin.subscriptionUsage = "upload=20;download=40;total=100";
    runtime.updateProfile(id);
    if (!check(done() && error().isEmpty() &&
                   profile().value("updatedAt") == now.toString(Qt::ISODate) &&
                   profile().value("usageCollectedAt") == now.toString(Qt::ISODate) &&
                   profile().value("usage") != usageAtImport,
               "304 with usage refreshes both timestamps and the sample"))
        return 41;
    if (!std::holds_alternative<std::monostate>(
            store.writeBytes(profile().value("sourceRef").toString(), "damaged")))
        return 7;
    const auto beforeRepair = origin.requests.size();
    runtime.updateProfile(id);
    if (!check(done() && error().isEmpty() && origin.requests.size() == beforeRepair + 2 &&
                   !origin.requestHeaders.last().toObject().contains("if-none-match") &&
                   std::holds_alternative<std::monostate>(store.verifyBytes(
                       profile().value("sourceRef").toString(), originalHash.toString())),
               "damaged cache forces one unconditional repair"))
        return 8;
    const auto beforeMissing = origin.requests.size();
    if (!std::holds_alternative<std::monostate>(
            store.remove(profile().value("sourceRef").toString())))
        return 26;
    runtime.updateProfile(id);
    if (!check(done() && error().isEmpty() && origin.requests.size() == beforeMissing + 2,
               "missing source forces unconditional retrieval"))
        return 27;
    origin.entityTags.remove("/subscription");
    origin.lastModified.insert("/subscription", "Wed, 28 Oct 2026 02:00:00 GMT");
    runtime.updateProfile(id);
    if (!done() || !error().isEmpty())
        return 28;
    runtime.updateProfile(id);
    if (!check(done() && error().isEmpty() &&
                   !origin.requestHeaders.last().toObject().contains("if-none-match") &&
                   origin.requestHeaders.last().toObject().contains("if-modified-since"),
               "Last-Modified alone supports 304"))
        return 29;
    origin.responseStatuses.insert("/always304", 304);
    auto before = origin.requests.size();
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/always304", {});
    if (!check(done() && error() == "SubscriptionCacheInvalid" &&
                   origin.requests.size() == before + 2 &&
                   profile().value("sourceHash") == originalHash,
               "repeated unsolicited 304 is rejected"))
        return 9;
    origin.redirects.insert("/same", "/subscription");
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/same",
                                "{\"Authorization\":\"Bearer public-test\"}");
    if (!check(done() && error().isEmpty() &&
                   origin.requestHeaders.last().toObject().value("authorization") ==
                       "Bearer public-test",
               "same-origin redirect keeps explicit authorization"))
        return 10;
    origin.redirects.insert(
        "/cross", QStringLiteral("https://127.0.0.1:%1/final").arg(other.serverPort()).toUtf8());
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/cross",
                                "{\"Authorization\":\"Bearer public-test\"}");
    if (!check(done() && error().isEmpty() &&
                   !other.requestHeaders.last().toObject().contains("authorization") &&
                   other.lastCookie.isEmpty(),
               "cross-origin redirect drops authorization and cookies"))
        return 11;
    const auto crossCount = other.requests.size();
    other.entityTags.insert("/final", "\"cross\"");
    runtime.updateProfile(id);
    if (!done() || !error().isEmpty())
        return 12;
    runtime.updateProfile(id);
    if (!check(done() && error().isEmpty() && other.requests.size() == crossCount + 2 &&
                   other.requestHeaders.last().toObject().value("if-none-match") == "\"cross\"" &&
                   !origin.requestHeaders.last().toObject().contains("if-none-match"),
               "validators only reach bound final URL"))
        return 13;
    for (int hop = 0; hop < 6; ++hop)
        origin.redirects.insert("/hop" + QByteArray::number(hop),
                                hop == 5 ? QByteArray("/subscription")
                                         : QByteArray("/hop") + QByteArray::number(hop + 1));
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/hop1", {});
    if (!check(done() && error().isEmpty(), "five HTTPS redirects allowed"))
        return 14;
    origin.redirects.insert("/loop", "/loop");
    origin.redirects.insert("/downgrade", "http://127.0.0.1/subscription");
    origin.redirects.insert("/login", "https://user:secret@127.0.0.1/subscription");
    for (const auto& path : {QStringLiteral("/hop0"), QStringLiteral("/loop"),
                             QStringLiteral("/downgrade"), QStringLiteral("/login")}) {
        runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + path, {});
        if (!check(done() && error() == "SubscriptionRedirectBlocked" &&
                       profile().value("sourceHash") == originalHash,
                   "unsafe redirect preserves source"))
            return 15;
    }
    runtime.changeProfileSource(id, source, {});
    if (!done() || !error().isEmpty())
        return 16;
    runtime.start(false);
    if (!check(done() && runtime.snapshot().value("ready").toBool(), "proxy core ready"))
        return 17;
    const auto initialSession = runtime.snapshot();
    origin.entityTags.remove("/subscription");
    origin.lastModified.remove("/subscription");
    const auto applyEquivalent = [&] {
        const auto before = profile();
        const auto beforeGeneration = runtime.snapshot().value("selectionGeneration");
        bool validatedByChild = false;
        const auto observe = QObject::connect(
            &runtime, &application::DesktopRuntime::changed, &runtime, [&](const QJsonObject&) {
                validatedByChild = validatedByChild || runtime.snapshot()
                                                           .value("configLifecycle")
                                                           .toObject()
                                                           .value("validationActive")
                                                           .toBool();
            });
        runtime.updateProfile(id, true);
        const bool completed = done();
        QObject::disconnect(observe);
        return completed && error().isEmpty() &&
               profile().value("sourceHash") != before.value("sourceHash") &&
               !profile().value("pendingApply").toBool() &&
               !profile().value("history").toArray().isEmpty() &&
               runtime.snapshot().value("selectionGeneration") != beforeGeneration &&
               runtime.snapshot().value("corePid") == initialSession.value("corePid") &&
               runtime.snapshot().value("epoch") == initialSession.value("epoch") &&
               runtime.snapshot().value("ready").toBool() && !validatedByChild;
    };
    origin.subscription += "# changed generation timestamp\n";
    if (!check(applyEquivalent(), "comment-only update reuses confirmed core and validation"))
        return 50;
    origin.subscription.replace("public_dummy", "public_changed");
    if (!check(applyEquivalent(), "ignored metadata update reuses confirmed core and validation"))
        return 51;
    origin.subscription.replace("public_changed", "public_dummy");
    if (!check(applyEquivalent(), "equivalent source restoration clears pending application"))
        return 52;
#ifdef Q_OS_WIN
    HANDLE manifestLock = INVALID_HANDLE_VALUE;
    const auto observeCommit = QObject::connect(
        &runtime, &application::DesktopRuntime::changed, &runtime, [&](const QJsonObject&) {
            if (manifestLock == INVALID_HANDLE_VALUE && profile().value("pendingApply").toBool()) {
                const auto path = store.root() + QStringLiteral("/data/runtime/current.json");
                manifestLock = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                           OPEN_EXISTING, 0, nullptr);
            }
        });
    const auto beforeCommitFailure = runtime.snapshot();
    origin.subscription += "# failed metadata transaction\n";
    runtime.updateProfile(id, true);
    const bool failedCommit = done();
    QObject::disconnect(observeCommit);
    const bool locked = manifestLock != INVALID_HANDLE_VALUE;
    if (locked)
        CloseHandle(manifestLock);
    if (!check(failedCommit && locked && !error().isEmpty() &&
                   profile().value("pendingApply").toBool() &&
                   profile().value("lastUpdateSaved").toBool() &&
                   runtime.snapshot().value("selectionGeneration") ==
                       beforeCommitFailure.value("selectionGeneration") &&
                   runtime.snapshot().value("corePid") == initialSession.value("corePid") &&
                   runtime.snapshot().value("epoch") == initialSession.value("epoch") &&
                   runtime.snapshot().value("ready").toBool(),
               "metadata commit failure preserves live core and saved pending candidate"))
        return 53;
    runtime.updateProfile(id, true);
    if (!check(done() && error().isEmpty() && !profile().value("pendingApply").toBool() &&
                   runtime.snapshot().value("corePid") == initialSession.value("corePid") &&
                   runtime.snapshot().value("epoch") == initialSession.value("epoch"),
               "unchanged pending candidate retries without replacing the core"))
        return 54;
#endif
    origin.subscription.replace("MATCH,upstream", "MATCH,DIRECT");
    runtime.updateProfile(id, true);
    if (!check(done() && error().isEmpty() && runtime.snapshot().value("ready").toBool() &&
                   runtime.snapshot().value("epoch") != initialSession.value("epoch") &&
                   !profile().value("pendingApply").toBool(),
               "effective rule change still applies a new core session"))
        return 55;
    origin.subscription = initialBody;
    origin.entityTags.insert("/subscription", "\"public-v1\"");
    origin.lastModified.insert("/subscription", "Wed, 28 Oct 2026 01:00:00 GMT");
    runtime.updateProfile(id, true);
    if (!check(done() && error().isEmpty() && runtime.snapshot().value("ready").toBool() &&
                   profile().value("sourceHash") == originalHash,
               "restore confirmed forwarding after effective update"))
        return 56;
    const auto pid = runtime.snapshot().value("corePid");
    runtime.setSubscriptionRoute(id, "RunningProxy");
    runtime.changeProfileSource(id, viaProxy, {});
    if (!check(done() && error().isEmpty() &&
                   profile().value("lastDownloadPath") == "RunningProxy" &&
                   !remote.targets.isEmpty() && runtime.snapshot().value("corePid") == pid,
               "download through owned mixed port preserves core"))
        return 18;
    runtime.setSubscriptionRoute(id, "Auto");
    runtime.updateProfile(id);
    if (!check(done() && error().isEmpty() &&
                   profile().value("lastDownloadPath") == "RunningProxy" &&
                   runtime.snapshot()
                       .value("features")
                       .toObject()
                       .value("subscriptionDownload")
                       .toObject()
                       .value("fallback")
                       .toBool(),
               "Auto retries direct network failure once through ready proxy"))
        return 19;
    origin.responseStatuses.insert("/denied", 403);
    before = origin.requests.size();
    const auto proxyCount = remote.targets.size();
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/denied", {});
    if (!check(done() && error() == "SubscriptionAuthenticationFailed" &&
                   origin.requests.size() == before + 1 && remote.targets.size() == proxyCount,
               "Auto never retries authentication rejection"))
        return 20;
    runtime.changeProfileSource(
        id, QStringLiteral("https://localhost:%1/subscription").arg(origin.serverPort()), {});
    if (!check(done() && error() == "SubscriptionDownloadFailed" &&
                   remote.targets.size() == proxyCount &&
                   profile().value("sourceHash") == originalHash &&
                   runtime.snapshot().value("corePid") == pid &&
                   runtime.snapshot().value("ready").toBool(),
               "Auto never retries TLS rejection"))
        return 21;
    origin.payloads.insert("/bad-format", "<!DOCTYPE html><title>sign in</title>");
    before = origin.requests.size();
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/bad-format", {});
    if (!check(done() && !error().isEmpty() && origin.requests.size() == before + 1 &&
                   remote.targets.size() == proxyCount,
               "Auto never retries invalid subscription content"))
        return 30;
    origin.responseDelays.insert("/deadline-first", 8000);
    origin.responseDelays.insert("/deadline-last", 14000);
    origin.redirects.insert("/deadline-first", "/deadline-last");
    QElapsedTimer chain;
    chain.start();
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/deadline-first", {});
    if (!check(done() && error() == "SubscriptionDownloadFailed" && chain.elapsed() >= 19500 &&
                   chain.elapsed() < 21000,
               "redirects share one twenty-second deadline"))
        return 31;
    origin.payloads.insert("/huge", QByteArray(16 * 1024 * 1024 + 1, 'x'));
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/huge", {});
    if (!check(done() && error() == "SubscriptionTooLarge" &&
                   profile().value("sourceHash") == originalHash &&
                   runtime.snapshot().value("corePid") == pid &&
                   runtime.snapshot().value("ready").toBool(),
               "streamed size bound preserves confirmed source and session"))
        return 22;
    origin.responseDelays.insert("/slow", 1000);
    before = origin.requests.size();
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/slow", {});
    if (!waitFor([&] { return origin.requests.size() > before; }))
        return 23;
    runtime.stop();
    QElapsedTimer cancelled;
    cancelled.start();
    waitFor([&] { return cancelled.elapsed() >= 1100; });
    if (!check(!runtime.snapshot().value("busy").toBool() &&
                   profile().value("sourceHash") == originalHash &&
                   runtime.snapshot()
                           .value("features")
                           .toObject()
                           .value("subscriptionDownload")
                           .toObject()
                           .value("state") == "Cancelled",
               "stop rejects late download"))
        return 24;
    {
        application::DesktopRuntime reopened(store);
        reopened.initialize();
        if (!check(reopened.snapshot()
                               .value("profiles")
                               .toArray()[0]
                               .toObject()
                               .value("downloadRoute") == "Auto" &&
                       reopened.snapshot()
                           .value("profiles")
                           .toArray()[0]
                           .toObject()
                           .value("compatibilityNotices")
                           .toArray()
                           .contains("ManagedGeo"),
                   "download policy survives reopen"))
            return 25;
    }
    auto cleanSubscription = origin.subscription;
    cleanSubscription.replace("x-vendor-node-id: public_dummy, ", "");
    cleanSubscription.replace("geox-url: {geoip: 'https://example.invalid/public-geo'}\n", "");
    origin.payloads.insert("/clean", cleanSubscription);
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/clean", {});
    if (!check(done() && error().isEmpty() &&
                   profile().value("compatibilityNotices").toArray().isEmpty(),
               "a clean revision clears old compatibility notices"))
        return 38;
    const auto cleanHash = profile().value("sourceHash");
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/bad-format", {});
    if (!check(done() && !error().isEmpty() && profile().value("sourceHash") == cleanHash &&
                   profile().value("compatibilityNotices").toArray().isEmpty(),
               "a failed update keeps the confirmed notices"))
        return 39;
    runtime.featureCommand(QStringLiteral("rollback_profile"),
                           {id, QStringLiteral("confirmed"), QStringLiteral("0")});
    if (!check(done() && error().isEmpty() &&
                   profile().value("compatibilityNotices").toArray().contains("ManagedGeo") &&
                   profile()
                       .value("compatibilityNotices")
                       .toArray()
                       .contains("IgnoredProxyExtensions"),
               "rollback recomputes compatibility notices from the restored revision"))
        return 40;
    before = origin.requests.size();
    runtime.changeProfileSource(id, source.left(source.lastIndexOf('/')) + "/slow", {});
    if (!waitFor([&] { return origin.requests.size() > before; }))
        return 32;
    runtime.deleteProfile(id);
    cancelled.restart();
    waitFor([&] { return cancelled.elapsed() >= 1100; });
    if (!check(!runtime.snapshot().value("busy").toBool() &&
                   runtime.snapshot().value("profiles").toArray().isEmpty(),
               "deleting an updating source cancels it and late replies never revive it"))
        return 33;
    origin.extraHeaders.insert("/subscription", "profile-title: Provider name\r\n");
    runtime.addSubscription(source, "  My name  ");
    if (!check(done() && error().isEmpty() && profile().value("name") == "My name",
               "explicit name overrides provider"))
        return 37;
    runtime.deleteProfile(profile().value("id").toString());
    origin.extraHeaders.remove("/subscription");
    runtime.addSubscription(source, {});
    if (!check(done() && error().isEmpty() && !profile().value("name").toString().isEmpty(),
               "missing title keeps generic fallback"))
        return 38;
    runtime.deleteProfile(profile().value("id").toString());
    other.extraHeaders.insert("/final", "Content-Disposition: attachment; "
                                        "filename*=UTF-8''%E6%9C%BA%E5%9C%BA.yaml\r\n");
    origin.redirects.insert(
        "/named", QStringLiteral("https://127.0.0.1:%1/final").arg(other.serverPort()).toUtf8());
    origin.extraHeaders.insert("/named", "profile-title: Ignore redirect metadata\r\n");
    runtime.addSubscription(source.left(source.lastIndexOf('/')) + "/named", {});
    if (!check(done() && error().isEmpty() && profile().value("name") == QString::fromUtf8("机场"),
               "only final successful response supplies filename"))
        return 39;
    runtime.deleteProfile(profile().value("id").toString());
    std::puts("subscription names, explicit URL reads, paths, redirects, validators, repair, "
              "bounds and cancellation "
              "passed");
    return 0;
}
} // namespace quickclash::test
