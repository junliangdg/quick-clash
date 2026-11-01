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
    application::DesktopRuntime runtime(store);
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
    origin.subscription = "proxies:\n- {name: fixture, type: http, server: 127.0.0.1, port: " +
                          QByteArray::number(remote.serverPort()) +
                          "}\nproxy-groups:\n- {name: upstream, type: select, proxies: [fixture, "
                          "DIRECT]}\nrules:\n- MATCH,upstream\n";
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
    runtime.addSubscription(source, "  ");
    if (!check(done() && error().isEmpty() && !profile().isEmpty(), "first direct import"))
        return 5;
    const auto id = profile().value("id").toString();
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
    const auto originalHash = profile().value("sourceHash");
    runtime.updateProfile(id);
    if (!check(done() && error().isEmpty() && profile().value("name") == "Cache fixture" &&
                   profile().value("sourceHash") == originalHash &&
                   origin.requestHeaders.last().toObject().value("if-none-match") ==
                       "\"public-v1\"" &&
                   origin.requestHeaders.last().toObject().contains("if-modified-since") &&
                   origin.lastCookie.isEmpty(),
               "304 reuses verified source and never sends cookies"))
        return 6;
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
                           .value("downloadRoute") == "Auto",
                   "download policy survives reopen"))
            return 25;
    }
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
