#include "custom_groups_probe.h"
#include "application/desktop_runtime.h"
#include "network_fixture.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <cstdio>
#include <yaml-cpp/yaml.h>

namespace quickclash::test {
namespace {
bool waitFor(const std::function<bool()>& condition, int limit = 20000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < limit) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(3);
    }
    return condition();
}
int port() {
    QTcpServer server;
    return server.listen(QHostAddress::LocalHost) ? server.serverPort() : 0;
}
class LocalProxy final : public QTcpServer {
  public:
    bool healthy = true;
    int requests = 0;
    LocalProxy() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QIODevice::readyRead, socket, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    if (!buffer->contains("\r\n\r\n"))
                        return;
                    if (buffer->startsWith("CONNECT ")) {
                        socket->write("HTTP/1.1 200 Connection established\r\n\r\n");
                        buffer->clear();
                        return;
                    }
                    ++requests;
                    if (!healthy) {
                        socket->abort();
                        buffer->clear();
                        return;
                    }
                    socket->write("HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: "
                                  "close\r\n\r\n");
                    socket->disconnectFromHost();
                    buffer->clear();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        listen(QHostAddress::LocalHost);
    }
};
} // namespace
int customGroupsProbe() {
    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (!std::holds_alternative<std::monostate>(store.open()))
        return 1;
    application::DesktopRuntime runtime(store);
    runtime.initialize();
    int mixed = port(), controller = port();
    while (controller == mixed)
        controller = port();
    runtime.setPorts(mixed, controller);
    LocalProxy a, b;
    TlsFixture origin;
    if (!origin.initialize())
        return 2;
    origin.subscription =
        QStringLiteral(
            "proxies:\n- {name: HK-A, type: http, server: 127.0.0.1, port: %1}\n"
            "- {name: HK-B, type: http, server: 127.0.0.1, port: %2}\n"
            "- {name: JP-A, type: http, server: 127.0.0.1, port: %2}\n"
            "proxy-groups:\n- {name: manual, type: select, proxies: [DIRECT, HK-A, HK-B, JP-A]}\n"
            "rules:\n- MATCH,manual\n")
            .arg(a.serverPort())
            .arg(b.serverPort())
            .toUtf8();
    const auto original = origin.subscription;
    QJsonObject view;
    QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                     [&view](const application::RuntimeUpdate& update) {
                         if (update.values.contains("custom_groups"))
                             view = update.values.value("custom_groups").toObject();
                     });
    const auto done = [&] {
        const bool idle = waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 30) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        return idle;
    };
    const auto check = [&](bool ok, const char* message) {
        if (!ok)
            std::fprintf(stderr, "Custom groups: %s\n%s\n", message,
                         QJsonDocument(runtime.snapshot()).toJson().constData());
        if (ok) {
            std::printf("Custom groups: %s passed\n", message);
            std::fflush(stdout);
        }
        return ok;
    };
    const auto catalog = [&] {
        return std::get<std::optional<QJsonObject>>(store.read("data/catalog.json"))
            .value()
            .value("profiles")
            .toArray()
            .first()
            .toObject();
    };
    const auto group = [&](const QString& name) {
        for (const auto& value : runtime.snapshot().value("groups").toArray())
            if (value.toObject().value("name") == name)
                return value.toObject();
        return QJsonObject{};
    };
    runtime.addSubscription(
        QStringLiteral("https://127.0.0.1:%1/subscription").arg(origin.serverPort()), "Groups");
    if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 1, "import"))
        return 3;
    const auto id = catalog().value("id").toString();
    runtime.setDemands({"proxies", "providers", "custom_groups"});
    if (!check(waitFor([&] { return !view.value("revision").toString().isEmpty(); }),
               "group context"))
        return 4;
    QJsonObject spec{{"id", ""},
                     {"name", "HK auto"},
                     {"type", "url-test"},
                     {"inline", true},
                     {"providers", QJsonArray{}},
                     {"mode", "keywords"},
                     {"include", "HK"},
                     {"exclude", "trial"},
                     {"url", "http://probe.invalid/health"},
                     {"interval", 1},
                     {"timeout", 500},
                     {"tolerance", 0},
                     {"lazy", false},
                     {"strategy", "consistent-hashing"},
                     {"parent", "manual"}};
    const auto preview = [&](const QJsonObject& definition, bool activate = false,
                             bool remove = false) {
        runtime.featureCommand(
            "custom_group_preview",
            {view.value("revision").toString(),
             QString::fromUtf8(QJsonDocument(definition).toJson(QJsonDocument::Compact)),
             remove ? "delete" : "save", activate ? "true" : "false"});
        return done() && view.value("preview").toObject().value("validated") == true;
    };
    const auto apply = [&] {
        const auto token = view.value("preview").toObject().value("token").toString();
        runtime.featureCommand("custom_group_apply", {token, "confirmed"});
        return done() && runtime.snapshot().value("error").toString().isEmpty();
    };
    runtime.featureCommand("custom_group_preview",
                           {view.value("revision").toString(),
                            QString::fromUtf8(QJsonDocument(spec).toJson(QJsonDocument::Compact)),
                            "save", "false"});
    runtime.setDemands({"proxies", "providers"});
    if (!check(!runtime.snapshot().value("busy").toBool() &&
                   !runtime.snapshot()
                        .value("configLifecycle")
                        .toObject()
                        .value("validationActive")
                        .toBool() &&
                   catalog().value("customGroups").toArray().isEmpty(),
               "release cancels in-flight preview validator without writes"))
        return 43;
    runtime.setDemands({"proxies", "providers", "custom_groups"});
    if (!check(waitFor([&] {
                   return !runtime.snapshot()
                               .value("customGroups")
                               .toObject()
                               .value("revision")
                               .toString()
                               .isEmpty();
               }),
               "reacquire custom group context after cancellation"))
        return 44;
    done();
    if (!check(preview(spec, true) && catalog().value("customGroups").toArray().isEmpty(),
               "official preview without writes"))
        return 5;
    if (!check(apply() && catalog().value("customGroups").toArray().size() == 1 &&
                   std::get<QByteArray>(store.readBytes(catalog().value("sourceRef").toString())) ==
                       original,
               "apply persists layer, preserves subscription bytes"))
        return 6;
    if (!check(waitFor([&] { return group("manual").value("current") == "HK auto"; }),
               "explicit parent selection offline"))
        return 7;
    // Test-only trust for the public loopback certificate; never change OS trust or import
    // policy. Mirrors the existing provider probe's isolated materialized-session fixture.
    QObject::connect(
        &runtime, &application::DesktopRuntime::changed, [&](const QJsonObject& state) {
            if (state.value("status") != "Starting" && state.value("status") != "Reloading")
                return;
            const auto listed = store.files("data/sessions");
            if (!std::holds_alternative<QStringList>(listed))
                return;
            const auto files = std::get<QStringList>(listed);
            QString reference;
            for (const auto& file : files)
                if (reference.isEmpty() ||
                    QFileInfo(directory.filePath(file)).lastModified() >
                        QFileInfo(directory.filePath(reference)).lastModified())
                    reference = file;
            QFile config(directory.filePath(reference));
            QFile cert("tests/fixtures/network/server_cert.pem");
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
    runtime.start(false);
    if (!check(done() && runtime.snapshot().value("ready").toBool() &&
                   waitFor([&] { return a.requests > 0 && b.requests > 0; }),
               "real core automatic probes"))
        return 8;
    runtime.refresh();
    if (!check(waitFor([&] {
                   return group("HK auto").value("current").toString().startsWith("HK-");
               }) &&
                   !group("HK auto").value("members").toArray().contains("JP-A"),
               "core candidates and current"))
        return 9;
    runtime.selectProxy("manual", "HK auto");
    if (!check(done() && waitFor([&] { return group("manual").value("current") == "HK auto"; }),
               "local group selected as parent outbound"))
        return 50;
    const auto savedDefinitions = catalog().value("customGroups");
    const auto replacementUrl =
        QStringLiteral("https://127.0.0.1:%1/replacement").arg(origin.serverPort());
    origin.payloads["/replacement"] = original + "# replacement address\n";
    // Changing the URL happens on the subscription page, where the proxy and
    // custom-group channels are released and must be reacquired on return.
    runtime.setDemands({});
    runtime.changeProfileSource(id, replacementUrl, {}, true);
    if (!check(done() && runtime.snapshot().value("error").toString().isEmpty() &&
                   runtime.snapshot().value("selectedId") == id &&
                   catalog().value("customGroups") == savedDefinitions,
               "replacing subscription retains local group definitions"))
        return 46;
    runtime.setDemands({"proxies", "providers", "custom_groups"});
    if (!check(waitFor([&] { return view.value("definitions") == savedDefinitions; }) &&
                   waitFor([&] {
                       return group("HK auto").value("members").toArray().contains("HK-A");
                   }) &&
                   group("HK auto").value("members").toArray().contains("HK-A") &&
                   group("manual").value("members").toArray().contains("HK auto") &&
                   group("manual").value("current") == "HK auto",
               "returning to proxy page shows and applies retained groups"))
        return 49;
    const auto priorSource = catalog().value("sourceRef");
    const auto priorSecret = catalog().value("secretRef");
    const auto priorPid = runtime.snapshot().value("corePid");
    origin.payloads["/incompatible"] = original;
    origin.payloads["/incompatible"].replace("manual", "new-selector");
    runtime.changeProfileSource(
        id, QStringLiteral("https://127.0.0.1:%1/incompatible").arg(origin.serverPort()), {}, true);
    if (!check(done() && runtime.snapshot().value("error") == "CustomGroupParentMissing" &&
                   catalog().value("sourceRef") == priorSource &&
                   catalog().value("secretRef") == priorSecret &&
                   catalog().value("customGroups") == savedDefinitions &&
                   runtime.snapshot().value("corePid") == priorPid && !group("HK auto").isEmpty(),
               "incompatible replacement preserves the original source and local groups"))
        return 47;
    runtime.changeProfileSource(
        id, QStringLiteral("https://127.0.0.1:%1/subscription").arg(origin.serverPort()), {}, true);
    if (!check(done() && runtime.snapshot().value("error").toString().isEmpty() &&
                   catalog().value("customGroups") == savedDefinitions &&
                   !group("HK auto").isEmpty(),
               "restoring source retains local groups"))
        return 48;
    origin.payloads["/other"] = original;
    origin.payloads["/other"].replace("manual", "other-selector");
    runtime.addSubscription(QStringLiteral("https://127.0.0.1:%1/other").arg(origin.serverPort()),
                            "Other subscription");
    if (!check(done() && runtime.snapshot().value("error").toString().isEmpty() &&
                   runtime.snapshot().value("profiles").toArray().size() == 2 &&
                   runtime.snapshot().value("selectedId") == id,
               "adding a second subscription retains the current one"))
        return 52;
    const auto otherId =
        runtime.snapshot().value("profiles").toArray().last().toObject().value("id").toString();
    runtime.setDemands({});
    runtime.selectProfile(otherId);
    if (!check(done() && runtime.snapshot().value("error").toString().isEmpty() &&
                   runtime.snapshot().value("ready").toBool() &&
                   runtime.snapshot().value("selectedId") == otherId &&
                   catalog().value("customGroups") == savedDefinitions,
               "switching subscriptions retains the original local groups"))
        return 53;
    runtime.selectProfile(id);
    if (!check(done() && runtime.snapshot().value("error").toString().isEmpty() &&
                   runtime.snapshot().value("ready").toBool() &&
                   runtime.snapshot().value("selectedId") == id,
               "switching back applies the original subscription"))
        return 54;
    runtime.setDemands({"proxies", "providers", "custom_groups"});
    if (!check(
            waitFor([&] {
                return runtime.snapshot().value("customGroups").toObject().value("definitions") ==
                           savedDefinitions &&
                       group("manual").value("current") == "HK auto" &&
                       group("HK auto").value("members").toArray().contains("HK-A");
            }),
            "returning to the original subscription restores its group and selection"))
        return 55;
    runtime.deleteProfile(otherId);
    if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 1 &&
                   runtime.snapshot().value("selectedId") == id,
               "remove temporary second subscription"))
        return 56;
    spec = catalog().value("customGroups").toArray().first().toObject();
    spec.insert("type", "fallback");
    if (!check(preview(spec, true) && apply(), "edit fallback"))
        return 10;
    a.healthy = false;
    QElapsedTimer refreshClock;
    refreshClock.start();
    if (!check(waitFor([&] {
                   if (refreshClock.elapsed() > 250) {
                       runtime.refresh();
                       refreshClock.restart();
                   }
                   return group("HK auto").value("current") == "HK-B";
               }),
               "real fallback switches away from unhealthy first node"))
        return 11;
    const auto beforeRevision = view.value("revision").toString();
    if (!check(preview(spec), "prepare stale preview"))
        return 12;
    const auto stale = view.value("preview").toObject().value("token").toString();
    origin.subscription.replace("HK-B", "HK-new");
    runtime.updateProfile(id);
    if (!check(done() && view.value("revision") != beforeRevision &&
                   group("HK auto").value("members").toArray().contains("HK-new"),
               "subscription update recomputes candidates"))
        return 13;
    runtime.featureCommand("custom_group_apply", {stale, "confirmed"});
    if (!check(runtime.snapshot().value("error") == "CustomGroupPreviewRequired",
               "stale preview rejected"))
        return 14;
    auto empty = spec;
    empty.insert("include", "missing-region");
    if (!check(!preview(empty) && view.value("preview").toObject().value("empty") == true,
               "empty new edit not applicable"))
        return 15;
    // A subscription can legitimately remove all candidates. Keep a real rejecting group.
    origin.subscription.replace("HK-A", "US-A");
    origin.subscription.replace("HK-new", "US-new");
    runtime.updateProfile(id);
    if (!check(done() && group("HK auto").value("members").toArray() == QJsonArray{"REJECT"},
               "empty updated group rejects, never DIRECT"))
        return 16;
    origin.subscription = original;
    runtime.updateProfile(id);
    if (!check(done(), "restore source"))
        return 17;
    // Strategy edits and retained history must include the local definition layer.
    const auto savedFallback = catalog().value("customGroups");
    spec = savedFallback.toArray().first().toObject();
    auto balancing = spec;
    balancing.insert("type", "load-balance");
    balancing.insert("strategy", "round-robin");
    if (!check(preview(balancing) && apply() && group("HK auto").value("type") == "LoadBalance",
               "real core load-balance edit"))
        return 30;
    runtime.featureCommand("rollback_profile",
                           {id, "confirmed", "0", catalog().value("revision").toString()});
    if (!check(done() && catalog().value("customGroups") == savedFallback &&
                   group("HK auto").value("type") == "Fallback",
               "history restores group definitions"))
        return 31;
    runtime.featureCommand("backup", {"confirmed", "source"});
    const auto backupUrl =
        runtime.snapshot().value("features").toObject().value("backupUrl").toString();
    runtime.featureCommand("preview_backup", {backupUrl});
    const auto backupToken = runtime.snapshot()
                                 .value("features")
                                 .toObject()
                                 .value("backupPreview")
                                 .toObject()
                                 .value("token")
                                 .toString();
    if (!check(!backupToken.isEmpty(), "group-aware backup preview"))
        return 18;
    runtime.featureCommand("restore_backup", {backupToken, "confirmed"});
    if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 2,
               "group-aware backup restore"))
        return 19;
    const auto restoredCatalog =
        std::get<std::optional<QJsonObject>>(store.read("data/catalog.json"))
            .value()
            .value("profiles")
            .toArray()
            .last()
            .toObject();
    if (!check(restoredCatalog.value("customGroups")
                   .toArray()
                   .first()
                   .toObject()
                   .contains("sourceNames"),
               "restored archive freezes source scope"))
        return 32;
    runtime.featureCommand("backup", {"confirmed", "offline"});
    const auto offlineUrl =
        runtime.snapshot().value("features").toObject().value("backupUrl").toString();
    runtime.featureCommand("preview_backup", {offlineUrl});
    const auto offlineToken = runtime.snapshot()
                                  .value("features")
                                  .toObject()
                                  .value("backupPreview")
                                  .toObject()
                                  .value("token")
                                  .toString();
    runtime.featureCommand("restore_backup", {offlineToken, "confirmed"});
    if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 4,
               "offline group backup restores without duplicating generated groups"))
        return 33;
    // Provider sources retain native use/filter, including a zero-match live update.
    origin.payloads["/nodes"] =
        QStringLiteral("proxies:\n- {name: HK-cloud, type: http, server: 127.0.0.1, port: %1}\n- "
                       "{name: JP-cloud, type: http, server: 127.0.0.1, port: %1}\n")
            .arg(b.serverPort())
            .toUtf8();
    origin.subscription =
        original + QStringLiteral("proxy-providers:\n  cloud:\n    type: http\n    url: "
                                  "https://127.0.0.1:%1/nodes\n    interval: 0\n")
                       .arg(origin.serverPort())
                       .toUtf8();
    runtime.updateProfile(id);
    if (!check(done(), "provider source download"))
        return 20;
    runtime.featureCommand("provider_mode", {id, "runtime"});
    if (!check(done() && catalog().value("providerMode") == "runtime", "provider runtime mode"))
        return 21;
    auto provider = spec;
    provider.insert("id", "");
    provider.insert("name", "Cloud HK");
    provider.insert("inline", false);
    provider.insert("exclude", "REJECT");
    provider.insert("providers", QJsonArray{"cloud"});
    provider.insert("type", "url-test");
    if (!check(preview(provider) && apply(), "native provider filter official load"))
        return 22;
    runtime.refresh();
    if (!check(waitFor([&] {
                   return group("Cloud HK").value("members").toArray().contains("HK-cloud");
               }) &&
                   !group("Cloud HK").value("members").toArray().contains("JP-cloud"),
               "provider filter membership"))
        return 23;
    // The loopback HTTP probe stubs do not forward arbitrary TLS traffic. Provider downloads
    // follow core routing; leave the tested automatic route before a real HTTPS refresh.
    runtime.selectProxy("manual", "DIRECT");
    if (!check(done(), "direct fixture route for provider download"))
        return 36;
    origin.payloads["/nodes"].replace("HK-cloud", "US-cloud");
    if (!check(waitFor([&] {
                   return !runtime.snapshot().value("providers").toArray().isEmpty() &&
                          runtime.snapshot().value("providerPage").toObject().value("loading") !=
                              true;
               }),
               "provider refresh controls ready"))
        return 34;
    origin.requests = {};
    runtime.featureCommand("update_provider", {"proxies", "cloud"});
    if (!check(done() && origin.requests.contains("GET /nodes") &&
                   runtime.snapshot().value("error").toString().isEmpty(),
               "core refresh fetches new provider payload"))
        return 35;
    refreshClock.restart();
    if (!check(waitFor([&] {
                   if (refreshClock.elapsed() > 250) {
                       runtime.refresh();
                       refreshClock.restart();
                   }
                   return group("Cloud HK").value("members").toArray() == QJsonArray{"REJECT"};
               }),
               "live provider empty filter stays REJECT"))
        return 24;
    if (!check(waitFor([&] {
                   for (const auto& entry : view.value("groups").toArray())
                       if (entry.toObject().value("name") == "Cloud HK")
                           return entry.toObject().value("count") == 0;
                   return false;
               }),
               "live empty-provider warning uses current core candidates"))
        return 45;
    runtime.selectProxy("manual", "DIRECT");
    if (!check(done(), "leave group before deletion"))
        return 25;
    const auto cloud = catalog().value("customGroups").toArray().last().toObject();
    if (!check(preview(cloud, false, true) && apply() && group("Cloud HK").isEmpty(),
               "delete custom group"))
        return 26;
    QJsonObject editor;
    QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                     [&editor](const application::RuntimeUpdate& update) {
                         if (update.values.contains("editor"))
                             editor = update.values.value("editor").toObject();
                     });
    runtime.setDemands({"proxies", "custom_groups", "editor"});
    runtime.featureCommand("editor_open", {id, "overlay", "confirmed"});
    if (!check(waitFor([&] { return !editor.value("token").toString().isEmpty(); }),
               "overlay editor with local groups"))
        return 37;
    const auto editorToken = editor.value("token").toString();
    const auto draft = QStringLiteral("{\"set\":{\"log-level\":\"warning\"}}");
    const auto localDefinitions = catalog().value("customGroups");
    runtime.featureCommand("editor_preview", {editorToken, draft});
    if (!check(done() && editor.contains("previewDigest"), "overlay preview composes local groups"))
        return 38;
    runtime.featureCommand("editor_apply", {editorToken, draft, "", "confirmed"});
    if (!check(done() && catalog().value("customGroups") == localDefinitions &&
                   !group("HK auto").isEmpty(),
               "overlay apply retains local definitions"))
        return 39;
    runtime.featureCommand("editor_preview", {editorToken, draft});
    if (!check(done() && editor.contains("previewDigest"), "copy candidate validated"))
        return 40;
    runtime.featureCommand("editor_copy", {editorToken, draft, "Copied groups", "confirmed"});
    if (!check(done(), "editor copy completes"))
        return 42;
    const auto allProfiles = std::get<std::optional<QJsonObject>>(store.read("data/catalog.json"))
                                 .value()
                                 .value("profiles")
                                 .toArray();
    if (!check(done() && allProfiles.size() == 5 &&
                   allProfiles.last().toObject().value("customGroups") == localDefinitions,
               "editor copy preserves group definitions"))
        return 41;
    runtime.stop();
    if (!check(done(), "stop"))
        return 27;
    runtime.setDemands({});
    if (!check(runtime.snapshot().value("customGroups").toObject().isEmpty(),
               "release transient group data"))
        return 28;
    application::DesktopRuntime reopened(store);
    reopened.initialize();
    reopened.setDemands({"proxies", "custom_groups"});
    if (!check(waitFor([&] {
                   return reopened.snapshot()
                              .value("customGroups")
                              .toObject()
                              .value("definitions")
                              .toArray()
                              .size() == 1;
               }),
               "definitions survive restart"))
        return 29;
    const auto copiedId = allProfiles.last().toObject().value("id").toString();
    reopened.deleteProfile(id);
    const auto remainingProfiles =
        std::get<std::optional<QJsonObject>>(store.read("data/catalog.json"))
            .value()
            .value("profiles")
            .toArray();
    bool removed = true;
    bool copyRetained = false;
    for (const auto& value : remainingProfiles) {
        const auto item = value.toObject();
        removed = removed && item.value("id") != id;
        if (item.value("id") == copiedId)
            copyRetained = item.value("customGroups") == localDefinitions;
    }
    if (!check(reopened.snapshot().value("error").toString().isEmpty() && removed && copyRetained &&
                   remainingProfiles.size() == allProfiles.size() - 1,
               "deleting a subscription removes its groups without affecting another profile"))
        return 51;
    std::puts("custom groups runtime probe passed");
    return 0;
}
} // namespace quickclash::test
