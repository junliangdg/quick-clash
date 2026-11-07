#include "provider_probe.h"
#include "application/desktop_runtime.h"
#include "network_fixture.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <cstdio>
#include <yaml-cpp/yaml.h>
#ifdef Q_OS_WIN
#include "platform/process_memory.h"
#endif

namespace quickclash::test {
namespace {
bool waitFor(const std::function<bool()>& condition) {
    QElapsedTimer time;
    time.start();
    while (!condition() && time.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return condition();
}
int port() {
    QTcpServer server;
    return server.listen(QHostAddress::LocalHost) ? server.serverPort() : 0;
}
} // namespace
int providerProbe() {
    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (!std::holds_alternative<std::monostate>(store.open()))
        return 1;
    application::DesktopRuntime runtime(store);
    runtime.initialize();
    auto mixed = port(), controller = port();
    while (mixed == controller)
        controller = port();
    runtime.setPorts(mixed, controller);
    TlsFixture origin;
    if (!origin.initialize())
        return 2;
    origin.payloads.insert("/nodes",
                           "proxies: [{name: stable, type: http, server: 127.0.0.1, port: 9}]\n");
    origin.payloads.insert("/rules", "payload:\n  - DOMAIN,one.invalid\n");
    const auto address = QByteArray::number(origin.serverPort());
    origin.subscription =
        "proxy-providers:\n  nodes:\n    type: http\n    url: https://127.0.0.1:" + address +
        "/nodes\n    interval: 3600\nrule-providers:\n  domains:\n    type: http\n    behavior: "
        "classical\n    url: https://127.0.0.1:" +
        address +
        "/rules\n    interval: 3600\nproxy-groups:\n- {name: selected, type: select, use: "
        "[nodes]}\nrules:\n- RULE-SET,domains,selected\n- MATCH,DIRECT\n";
    origin.entityTags.insert("/subscription", "\"provider-main-v1\"");
    const auto check = [&](bool result, const char* message) {
        if (!result) {
            std::fprintf(stderr, "Provider assertion: %s\n", message);
            const auto state = runtime.snapshot();
            std::fprintf(stderr, "error=%s status=%s issues=%s\n",
                         state.value("error").toString().toUtf8().constData(),
                         state.value("status").toString().toUtf8().constData(),
                         QJsonDocument(state.value("configIssues").toArray())
                             .toJson(QJsonDocument::Compact)
                             .constData());
            std::fprintf(stderr, "providers=%s requests=%s\n",
                         QJsonDocument(state.value("providers").toArray())
                             .toJson(QJsonDocument::Compact)
                             .constData(),
                         QJsonDocument(origin.requests).toJson(QJsonDocument::Compact).constData());
        }
        return result;
    };
    const auto done = [&] {
        return waitFor([&] {
            const auto state = runtime.snapshot();
            return !state.value("busy").toBool() &&
                   (!state.value("ready").toBool() ||
                    !state.value("dataLifecycle")
                         .toObject()
                         .value("active")
                         .toArray()
                         .contains("providers") ||
                    (!state.value("providerPage")
                          .toObject()
                          .value("sampleAt")
                          .toString()
                          .isEmpty() &&
                     !state.value("providerPage").toObject().value("loading").toBool()));
        });
    };
    const auto catalog = [&] {
        return std::get<std::optional<QJsonObject>>(store.read("data/catalog.json"))
            ->value("profiles")
            .toArray()
            .first()
            .toObject();
    };
    runtime.addSubscription(
        QStringLiteral("https://127.0.0.1:%1/subscription").arg(origin.serverPort()),
        "Provider fixture");
    if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 1,
               "initial snapshot import"))
        return 3;
    const auto id = catalog().value("id").toString();
    runtime.start(false);
    if (!check(done() && runtime.snapshot().value("ready").toBool(), "snapshot ready"))
        return 4;
    runtime.setDemands({"providers"});
    if (!check(waitFor([&] { return runtime.snapshot().value("providers").toArray().size() >= 2; }),
               "provider metadata loaded"))
        return 5;
    const auto providerRow = [&](const QString& kind) {
        for (const auto& value : runtime.snapshot().value("providers").toArray())
            if (value.toObject().value("kind") == kind)
                return value.toObject();
        return QJsonObject{};
    };
    const auto resources = catalog().value("providerResources").toArray();
    if (!check(providerRow("proxy-providers").value("type") == "HTTP",
               "snapshot page includes the managed provider type"))
        return 48;
    if (!check(resources.size() == 2 && !runtime.snapshot()
                                             .value("profiles")
                                             .toArray()
                                             .first()
                                             .toObject()
                                             .contains("providerResources"),
               "cache references remain worker private"))
        return 6;
    for (const auto& value : resources) {
        const auto resource = value.toObject();
        if (!check(
                std::holds_alternative<std::monostate>(store.verifyBytes(
                    resource.value("sourceRef").toString(), resource.value("sha256").toString())),
                "cache digest retained after garbage collection"))
            return 7;
    }
    origin.requests = {};
    const auto firstPid = runtime.snapshot().value("corePid");
    origin.payloads.insert("/nodes",
                           "proxies: [{name: stable, type: http, server: 127.0.0.1, port: 9}, "
                           "{name: added, type: socks5, server: 127.0.0.1, port: 10}]\n");
    runtime.featureCommand("update_provider", {"proxy-providers", "nodes"});
    if (!check(done() && origin.requests == QJsonArray{"GET /nodes"} &&
                   runtime.snapshot().value("ready").toBool() &&
                   runtime.snapshot().value("corePid") != firstPid,
               "single resource fetch rebuilds and applies snapshot without fetching main or other "
               "resource"))
        return 8;
    runtime.selectProxy("selected", "added");
    const auto chosen = [&] {
        for (const auto& value : runtime.snapshot().value("groups").toArray())
            if (value.toObject().value("name") == "selected")
                return value.toObject().value("current").toString();
        return QString{};
    };
    if (!check(waitFor([&] {
                   return !runtime.snapshot().value("busy").toBool() && chosen() == "added";
               }),
               "provider member selected and read back"))
        return 14;
    origin.payloads.insert("/nodes",
                           "proxies: [{name: stable, type: http, server: 127.0.0.1, port: 9}, "
                           "{name: added, type: socks5, server: 127.0.0.1, port: 10}, "
                           "{name: extra, type: http, server: 127.0.0.1, port: 11}]\n");
    runtime.featureCommand("update_provider", {"proxy-providers", "nodes"});
    if (!check(done() && chosen() == "added",
               "unrelated node addition preserves verified identity"))
        return 15;
    origin.payloads.insert("/nodes",
                           "proxies: [{name: stable, type: http, server: 127.0.0.1, port: 9}, "
                           "{name: added, type: socks5, server: 127.0.0.1, port: 12}]\n");
    runtime.featureCommand("update_provider", {"proxy-providers", "nodes"});
    if (!check(done() && chosen() == "stable",
               "changed endpoint under same name never restores old selection"))
        return 16;
    runtime.selectProxy("selected", "added");
    if (!check(waitFor([&] {
                   return !runtime.snapshot().value("busy").toBool() && chosen() == "added";
               }),
               "replacement member selected before removal"))
        return 55;
    origin.payloads.insert("/nodes",
                           "proxies: [{name: stable, type: http, server: 127.0.0.1, port: 9}]\n");
    runtime.featureCommand("update_provider", {"proxy-providers", "nodes"});
    if (!check(done() && chosen() == "stable", "removed member never restores stale selection"))
        return 56;
    runtime.configureSubscription(id, 0, false);
    const auto unappliedPid = runtime.snapshot().value("corePid");
    origin.payloads.insert("/rules", "payload: ['DOMAIN,two.invalid']\n");
    origin.requests = {};
    runtime.featureCommand("update_provider", {"rule-providers", "domains"});
    if (!check(done() && origin.requests == QJsonArray{"GET /rules"} &&
                   runtime.snapshot().value("corePid") == unappliedPid &&
                   catalog().value("pendingApply").toBool(),
               "rule resource update obeys independent automatic apply preference"))
        return 17;
    if (!check(waitFor([&] {
                   for (const auto& value : runtime.snapshot().value("providers").toArray())
                       if (value.toObject().value("kind") == "rule-providers")
                           return value.toObject().value("lastResult") == "PendingApply";
                   return false;
               }),
               "snapshot result distinguishes pending application"))
        return 42;
    runtime.configureSubscription(id, 0, true);
    runtime.selectProfile(id);
    if (!check(done() && runtime.snapshot().value("ready").toBool(),
               "pending provider snapshot applies normally"))
        return 18;
    const auto goodHash = catalog().value("snapshotHash");
    const auto goodPid = runtime.snapshot().value("corePid");
    const auto goodFiles = std::get<QStringList>(store.files("data/profiles"));
    origin.payloads.insert("/nodes", "invalid");
    runtime.featureCommand("update_provider", {"proxy-providers", "nodes"});
    if (!check(done() && catalog().value("snapshotHash") == goodHash &&
                   runtime.snapshot().value("corePid") == goodPid &&
                   std::get<QStringList>(store.files("data/profiles")) == goodFiles,
               "bad provider preserves catalog, files and running session"))
        return 9;
    origin.payloads.insert("/nodes",
                           "proxies: [{name: stable, type: http, server: 127.0.0.1, port: 9}]\n");
    // Break the resource that this single update must reuse, never silently refetch it.
    QString cachePath;
    for (const auto& value : catalog().value("providerResources").toArray())
        if (value.toObject().value("kind") == "rule-providers")
            cachePath =
                std::get<QString>(store.path(value.toObject().value("sourceRef").toString()));
    QFile cache(cachePath);
    if (!cache.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 10;
    cache.write("corrupt");
    cache.close();
    origin.requests = {};
    runtime.featureCommand("update_provider", {"proxy-providers", "nodes"});
    if (!check(done() && runtime.snapshot().value("error") == "ProviderCacheUnavailable" &&
                   runtime.snapshot().value("corePid") == goodPid &&
                   catalog().value("snapshotHash") == goodHash &&
                   !origin.requests.contains("GET /rules"),
               "missing or corrupt reusable cache fails without replacing active snapshot"))
        return 11;
    origin.requests = {};
    origin.requestHeaders = {};
    runtime.updateProfile(id);
    if (!check(done() && runtime.snapshot().value("ready").toBool() &&
                   origin.requestHeaders.first().toObject().value("if-none-match") ==
                       "\"provider-main-v1\"" &&
                   origin.requests.contains("GET /nodes") && origin.requests.contains("GET /rules"),
               "304 main source still refreshes and repairs provider resources"))
        return 12;
    runtime.configureSubscription(id, 0, false);
    const auto snapshotBeforePending = providerRow("proxy-providers").value("bytes");
    const auto coreBeforePending = runtime.snapshot().value("corePid");
    origin.payloads.insert("/nodes",
                           "proxies: [{name: stable, type: http, server: 127.0.0.1, port: 9}, "
                           "{name: pending, type: http, server: 127.0.0.1, port: 10}]\n");
    runtime.updateProfile(id);
    if (!check(done() && waitFor([&] {
                   return providerRow("proxy-providers").value("bytes") != snapshotBeforePending;
               }) &&
                   runtime.snapshot().value("corePid") == coreBeforePending &&
                   catalog().value("pendingApply").toBool(),
               "catalog update refreshes selected snapshot metadata while old core stays active"))
        return 49;
    runtime.configureSubscription(id, 0, true);
    origin.responseDelays.insert("/nodes", 500);
    const auto beforeCancel = catalog().value("snapshotHash");
    runtime.featureCommand("update_provider", {"proxy-providers", "nodes"});
    runtime.stop();
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 700) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    if (!check(!runtime.snapshot().value("busy").toBool() &&
                   !runtime.snapshot().value("ready").toBool() &&
                   catalog().value("snapshotHash") == beforeCancel,
               "stop rejects late provider download"))
        return 13;
    // Trust the public loopback fixture only inside this test core's materialized session.
    // Production preflight continues to reject tls/custom certificates; OS trust is untouched.
    bool trustFixture = true;
    QObject::connect(&runtime, &application::DesktopRuntime::changed,
                     [&](const QJsonObject& state) {
                         if (!trustFixture || state.value("status") != "Starting")
                             return;
                         const auto path = directory.path() + "/data/sessions/" +
                                           state.value("epoch").toString() + "/config.yaml";
                         QFile config(path), cert("tests/fixtures/network/server_cert.pem");
                         if (!config.open(QIODevice::ReadOnly) || !cert.open(QIODevice::ReadOnly))
                             return;
                         auto root = YAML::Load(config.readAll().toStdString());
                         root["tls"]["custom-certifactes"].push_back(cert.readAll().toStdString());
                         config.close();
                         YAML::Emitter output;
                         output << root;
                         if (config.open(QIODevice::WriteOnly | QIODevice::Truncate))
                             config.write(output.c_str(), static_cast<qint64>(output.size()));
                     });
    origin.responseDelays.clear();
    runtime.featureCommand("provider_mode", {id, "runtime"});
    if (!check(done() && catalog().value("providerMode") == "runtime", "runtime mode saved"))
        return 19;
    const auto dynamicYaml =
        std::get<QByteArray>(store.readBytes(catalog().value("snapshotRef").toString()));
    if (!check(dynamicYaml.contains("RULE-SET") && dynamicYaml.contains("use:") &&
                   dynamicYaml.contains("providers/") && dynamicYaml.contains("payload:"),
               "runtime references and import fallback retained"))
        return 20;
    runtime.start(false);
    if (!check(done() && runtime.snapshot().value("ready").toBool(), "runtime provider ready"))
        return 21;
    runtime.setDemands({"providers"});
    if (!check(waitFor([&] {
                   const auto providers = runtime.snapshot().value("providers").toArray();
                   return providers.size() == 2 &&
                          providers.first().toObject().value("mode") == "runtime";
               }),
               "only managed HTTP runtime providers displayed"))
        return 22;
    const auto runtimePid = runtime.snapshot().value("corePid");
    const auto sourceHash = catalog().value("snapshotHash");
    origin.payloads.insert("/nodes",
                           "proxies: [{name: stable, type: http, server: 127.0.0.1, port: 9}, "
                           "{name: dynamic, type: socks5, server: 127.0.0.1, port: 10}]\n");
    origin.requests = {};
    runtime.featureCommand("update_provider", {"proxies", "nodes"});
    const auto hasDynamic = [&] {
        for (const auto& value : runtime.snapshot().value("groups").toArray())
            if (value.toObject().value("name") == "selected")
                return value.toObject().value("members").toArray().contains("dynamic");
        return false;
    };
    if (!check(done() && waitFor(hasDynamic) && origin.requests == QJsonArray{"GET /nodes"} &&
                   runtime.snapshot().value("corePid") == runtimePid &&
                   catalog().value("snapshotHash") == sourceHash,
               "official core hot refresh changes members without restart or snapshot write"))
        return 23;
    runtime.selectProxy("selected", "dynamic");
    if (!check(waitFor([&] {
                   return !runtime.snapshot().value("busy").toBool() && chosen() == "dynamic";
               }),
               "dynamic selection read back"))
        return 24;
    const auto selections =
        std::get<std::optional<QJsonObject>>(store.read("data/node_selections.json"));
    if (!check(selections && !selections->value("groups").toObject().contains("selected"),
               "dynamic name is not persisted as a verified identity"))
        return 25;
    const auto cacheFiles = std::get<QStringList>(store.files("data/core_state/providers"));
    if (!check(cacheFiles.size() == 2, "official core writes separate bounded provider caches"))
        return 37;
    QHash<QString, QByteArray> cacheBodies;
    for (const auto& file : cacheFiles)
        cacheBodies.insert(file, std::get<QByteArray>(store.readBytes(file)));
    origin.payloads.insert("/nodes", "invalid");
    runtime.featureCommand("update_provider", {"proxies", "nodes"});
    if (!check(done() && !runtime.snapshot().value("error").toString().isEmpty() && hasDynamic() &&
                   runtime.snapshot().value("corePid") == runtimePid,
               "bad runtime payload preserves live members and process"))
        return 26;
    if (!check(waitFor([&] {
                   for (const auto& value : runtime.snapshot().value("providers").toArray())
                       if (value.toObject().value("kind") == "proxies")
                           return value.toObject().value("lastResult") == "Failed";
                   return false;
               }),
               "runtime update failure belongs to one provider row"))
        return 43;
    for (auto it = cacheBodies.begin(); it != cacheBodies.end(); ++it)
        if (!check(std::get<QByteArray>(store.readBytes(it.key())) == it.value(),
                   "bad runtime payload never replaces core cache"))
            return 27;
    origin.payloads.insert("/rules",
                           "payload:\n  - DOMAIN,hot.invalid\n  - DOMAIN,second.invalid\n");
    origin.requests = {};
    runtime.featureCommand("update_provider", {"rules", "domains"});
    if (!check(done() && runtime.snapshot().value("corePid") == runtimePid &&
                   origin.requests == QJsonArray{"GET /rules"} && waitFor([&] {
                       for (const auto& value : runtime.snapshot().value("providers").toArray())
                           if (value.toObject().value("kind") == "rules")
                               return value.toObject().value("count").toInt() == 2;
                       return false;
                   }),
               "rule provider hot refresh reads updated rule count"))
        return 38;
    runtime.setDemands({"rules"});
    if (!check(waitFor([&] { return runtime.snapshot().value("rules").toArray().size() == 2; }),
               "effective rules loaded on demand"))
        return 44;
    const auto liveRules = runtime.snapshot().value("rules").toArray();
    if (!check(liveRules.first().toObject().value("position") == 1 &&
                   liveRules.first().toObject().value("providerName") == "domains" &&
                   liveRules.first().toObject().value("count") == 2 &&
                   liveRules.last().toObject().value("position") == 2,
               "official core order and RULE-SET metadata are linked"))
        return 45;
    runtime.setDemands({"providers"});
    if (!check(
            waitFor([&] { return runtime.snapshot().value("providers").toArray().size() == 2; }) &&
                runtime.snapshot().value("rules").toArray().isEmpty() &&
                runtime.snapshot().value("rulePage").toObject().isEmpty(),
            "rule data and derived status released on section switch"))
        return 46;
    runtime.previewConfiguration();
    const auto preview = runtime.snapshot().value("effectivePreview").toString();
    if (!check(!preview.contains(QStringLiteral("https://127.0.0.1:%1").arg(origin.serverPort())) &&
                   !preview.contains("providers/") && !preview.contains("hot.invalid"),
               "runtime preview masks provider URLs and payloads"))
        return 39;
#ifdef Q_OS_WIN
    if (QCoreApplication::arguments().contains("--provider-memory")) {
        const auto baseline = platform::currentProcessMemory();
        auto peak = baseline;
        QElapsedTimer measure;
        measure.start();
        int iterations = 0;
        while (measure.elapsed() < 10000) {
            runtime.featureCommand("update_provider", {"rules", "domains"});
            if (!done() || runtime.snapshot().value("corePid") != runtimePid)
                return 40;
            const auto sample = platform::currentProcessMemory();
            for (const auto* key : {"privateWorkingSet", "workingSet", "privateCommit"})
                if (sample.value(key).toString().toULongLong() >
                    peak.value(key).toString().toULongLong())
                    peak.insert(key, sample.value(key));
            ++iterations;
            QCoreApplication::processEvents();
            QThread::msleep(100);
        }
        runtime.setDemands({});
        QCoreApplication::processEvents();
        if (!runtime.snapshot().value("providers").toArray().isEmpty() ||
            !runtime.snapshot().value("providerPage").toObject().isEmpty() ||
            runtime.snapshot()
                    .value("dataLifecycle")
                    .toObject()
                    .value("providerResultCount")
                    .toInt() != 0)
            return 47;
        const QJsonObject report{
            {"durationMs", QString::number(measure.elapsed())},
            {"iterations", iterations},
            {"baseline", baseline},
            {"peak", peak},
            {"released", platform::currentProcessMemory()},
            {"providerRowsAfterRelease", runtime.snapshot().value("providers").toArray().size()},
            {"cacheFiles", std::get<QStringList>(store.files("data/core_state/providers")).size()},
            {"core", platform::processMemory(
                         platform::processIdentity(static_cast<quint32>(runtimePid.toInt())))}};
        std::puts(QJsonDocument(report).toJson(QJsonDocument::Compact).constData());
    }
#endif
    origin.requests = {};
    for (const auto* scope : {"source", "offline"}) {
        runtime.featureCommand("backup", {"confirmed", scope});
        const auto url =
            runtime.snapshot().value("features").toObject().value("backupUrl").toString();
        QFile archive(QUrl(url).toLocalFile());
        if (!archive.open(QIODevice::ReadOnly))
            return 28;
        const auto entry = QJsonDocument::fromJson(archive.readAll())
                               .object()
                               .value("profiles")
                               .toArray()
                               .first()
                               .toObject();
        const auto fallback = QByteArray::fromBase64(
            entry.value(QString::fromLatin1(scope) == "source" ? "snapshotYaml" : "yaml")
                .toString()
                .toLatin1());
        if (!check(!fallback.contains("proxy-providers:") && !fallback.contains("RULE-SET,") &&
                       fallback.contains("stable") && !fallback.contains("dynamic"),
                   "backup exports immutable import fallback as offline snapshot"))
            return 29;
        runtime.featureCommand("preview_backup", {url});
        const auto token = runtime.snapshot()
                               .value("features")
                               .toObject()
                               .value("backupPreview")
                               .toObject()
                               .value("token")
                               .toString();
        if (!check(!token.isEmpty(), "runtime profile backup preview accepted"))
            return 30;
        if (QString::fromLatin1(scope) == "offline") {
            runtime.featureCommand("restore_backup", {token, "confirmed"});
            if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 2 &&
                           runtime.snapshot().value("corePid") == runtimePid,
                       "offline fallback backup restores without touching active core"))
                return 41;
        } else
            runtime.featureCommand("cancel_backup_restore", {});
    }
    if (!check(origin.requests.isEmpty(), "backup uses no remote provider access"))
        return 31;
    origin.payloads.insert("/nodes",
                           "proxies: [{name: stable, type: http, server: 127.0.0.1, port: 9}, "
                           "{name: dynamic, type: socks5, server: 127.0.0.1, port: 12}]\n");
    runtime.stop();
    runtime.start(false);
    if (!check(done() && chosen() == "stable",
               "restart does not restore an unverified dynamic selection"))
        return 32;
    runtime.setDemands({"providers"});
    if (!waitFor([&] { return runtime.snapshot().value("providers").toArray().size() == 2; }))
        return 33;
    origin.responseDelays.insert("/nodes", 500);
    runtime.featureCommand("update_provider", {"proxies", "nodes"});
    runtime.stop();
    settle.restart();
    while (settle.elapsed() < 700) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    if (!check(!runtime.snapshot().value("busy").toBool() &&
                   !runtime.snapshot().value("ready").toBool(),
               "runtime late reply cannot revive stopped session"))
        return 34;
    trustFixture = false;
    origin.responseDelays.clear();
    runtime.featureCommand("provider_mode", {id, "snapshot"});
    if (!check(done() && catalog().value("providerMode") == "snapshot", "return to snapshot mode"))
        return 35;
    runtime.start(false);
    if (!check(done() && runtime.snapshot().value("ready").toBool() &&
                   std::get<QStringList>(store.files("data/core_state/providers")).isEmpty(),
               "stopped-core cache collection removes inactive runtime providers"))
        return 36;
    runtime.stop();
    std::puts("Provider snapshot/runtime modes, fixed hot refresh, cache preservation, identity, "
              "backup fallback and cancellation passed");
    return 0;
}
} // namespace quickclash::test
