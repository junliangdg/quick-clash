#include "user_rules_probe.h"
#include "application/desktop_runtime.h"
#include "network_fixture.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <cstdio>
#include <functional>
#include <yaml-cpp/yaml.h>

namespace quickclash::test {
namespace {
bool waitFor(const std::function<bool()>& condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 20000) {
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
int userRulesProbe() {
    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (!std::holds_alternative<std::monostate>(store.open()))
        return 1;
    const auto check = [](bool value, const char* label) {
        if (!value)
            std::fprintf(stderr, "User rules: %s\n", label);
        return value;
    };
    const auto writeFixture = [&](const QString& name, const QString& group) {
        QFile file(directory.filePath(name));
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(QStringLiteral("proxies: []\nproxy-groups:\n- {name: %1, type: select, "
                                         "proxies: [DIRECT]}\nrules:\n- MATCH,%1\n")
                              .arg(group)
                              .toUtf8()) > 0;
    };
    if (!writeFixture("a.yaml", "Alpha") || !writeFixture("b.yaml", "Beta"))
        return 2;
    QString idA, idB;
    {
        application::DesktopRuntime runtime(store);
        QJsonObject view;
        QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                         [&view](const application::RuntimeUpdate& update) {
                             if (update.values.contains("user_rules"))
                                 view = update.values.value("user_rules").toObject();
                         });
        runtime.initialize();
        int mixed = port(), controller = port();
        while (mixed == controller)
            controller = port();
        runtime.setPorts(mixed, controller);
        const auto done = [&] {
            return waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
        };
        const auto selected = [&] { return runtime.snapshot().value("selectedId").toString(); };
        const auto context = [&](const QString& id) {
            return waitFor([&] {
                return view.value("profileId") == id &&
                       !view.value("revision").toString().isEmpty();
            });
        };
        runtime.importFile(QUrl::fromLocalFile(directory.filePath("a.yaml")).toString(), "A");
        if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 1,
                   "import A"))
            return 3;
        idA = runtime.snapshot()
                  .value("profiles")
                  .toArray()
                  .first()
                  .toObject()
                  .value("id")
                  .toString();
        runtime.importFile(QUrl::fromLocalFile(directory.filePath("b.yaml")).toString(), "B");
        if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 2,
                   "import B"))
            return 4;
        idB =
            runtime.snapshot().value("profiles").toArray().last().toObject().value("id").toString();
        if (selected() != idA)
            runtime.selectProfile(idA);
        if (!check(done() && selected() == idA, "select A"))
            return 5;
        runtime.setDemands({"user_rules"});
        if (!check(context(idA), "context A"))
            return 6;
        const auto save = [&](const QString& scope, const QJsonArray& rules, const QString& proxy) {
            const auto oldRevision = view.value("revision").toString();
            runtime.featureCommand(
                "user_rules_save",
                {oldRevision, scope, view.value("profileId").toString(),
                 QString::fromUtf8(QJsonDocument(rules).toJson(QJsonDocument::Compact)), proxy});
            return done() && waitFor([&] {
                       return (view.value("result") == "Failed" &&
                               !runtime.snapshot().value("error").toString().isEmpty()) ||
                              (view.value("result") == "Saved" &&
                               view.value("revision") != oldRevision);
                   });
        };
        const QJsonArray global{QJsonObject{{"id", "g"},
                                            {"type", "DOMAIN-SUFFIX"},
                                            {"value", "example.com"},
                                            {"target", "PROXY"},
                                            {"enabled", true}}};
        const QJsonArray local{QJsonObject{{"id", "l"},
                                           {"type", "DOMAIN"},
                                           {"value", "local.example.com"},
                                           {"target", "DIRECT"},
                                           {"enabled", true}}};
        if (!check(save("global", global, "Alpha") && view.value("result") == "Saved",
                   "save global and binding"))
            return 7;
        if (!check(save("local", local, "Alpha") && view.value("result") == "Saved", "save local"))
            return 8;
        const auto manifest = [&] {
            return std::get<std::optional<QJsonObject>>(store.read("data/runtime/current.json"))
                .value();
        };
        const auto readRules = [&] {
            const auto ref =
                manifest().value("selection").toObject().value("artifactRef").toString();
            const auto bytes = std::get<QByteArray>(store.readBytes(ref));
            return YAML::Load(bytes.toStdString())["rules"];
        };
        auto rules = readRules();
        if (!check(rules.size() == 3 &&
                       rules[0].as<std::string>() == "DOMAIN,local.example.com,DIRECT" &&
                       rules[1].as<std::string>() == "DOMAIN-SUFFIX,example.com,Alpha",
                   "local before global before original"))
            return 9;
        runtime.start(false);
        if (!check(done() && runtime.snapshot().value("ready").toBool(), "start A"))
            return 10;
        const auto before = manifest();
        const auto pid = runtime.snapshot().value("corePid");
        runtime.selectProfile(idB);
        const auto binding =
            runtime.snapshot().value("features").toObject().value("userRuleBinding").toObject();
        if (!check(done() && selected() == idA && runtime.snapshot().value("corePid") == pid &&
                       manifest() == before && binding.value("profileId") == idB &&
                       binding.value("groups").toArray().contains("Beta"),
                   "missing binding leaves running A untouched"))
            return 11;
        runtime.featureCommand("user_rules_bind", {"Beta"});
        if (!check(done() && selected() == idB && runtime.snapshot().value("ready").toBool() &&
                       context(idB),
                   "bind and switch B"))
            return 12;
        rules = readRules();
        if (!check(rules[0].as<std::string>() == "DOMAIN-SUFFIX,example.com,Beta",
                   "B uses its own proxy binding"))
            return 13;
        const auto beforeBad = manifest();
        const QJsonArray invalid{QJsonObject{{"id", "bad"},
                                             {"type", "IP-CIDR"},
                                             {"value", "not-a-network"},
                                             {"target", "DIRECT"},
                                             {"enabled", true}}};
        if (!check(save("local", invalid, "Beta") && view.value("result") == "Failed" &&
                       manifest() == beforeBad && runtime.snapshot().value("ready").toBool(),
                   "invalid edit preserves confirmed session"))
            return 14;
        runtime.selectProfile(idA);
        if (!check(done() && selected() == idA && context(idA), "return to A"))
            return 15;
        runtime.featureCommand("backup", {"confirmed", "source"});
        const auto backupUrl =
            runtime.snapshot().value("features").toObject().value("backupUrl").toString();
        QFile archive(QUrl(backupUrl).toLocalFile());
        if (!archive.open(QIODevice::ReadOnly))
            return 20;
        const auto backup = QJsonDocument::fromJson(archive.readAll()).object();
        if (!check(backup.value("schemaVersion") == 4 &&
                       backup.value("globalRules").toArray() == global &&
                       backup.value("profiles")
                               .toArray()
                               .first()
                               .toObject()
                               .value("userRules")
                               .toObject()
                               .value("rules")
                               .toArray() == local,
                   "backup stores separate global and profile rules"))
            return 21;
        auto changedGlobal = global;
        auto changedRule = changedGlobal.first().toObject();
        changedRule.insert("target", "DIRECT");
        changedGlobal[0] = changedRule;
        if (!check(save("global", changedGlobal, "Alpha") && view.value("result") == "Saved",
                   "change global before restore"))
            return 22;
        runtime.featureCommand("backup", {"confirmed", "offline"});
        const auto offlineUrl =
            runtime.snapshot().value("features").toObject().value("backupUrl").toString();
        QFile offline(QUrl(offlineUrl).toLocalFile());
        if (!offline.open(QIODevice::ReadOnly))
            return 53;
        const auto offlineArchive = QJsonDocument::fromJson(offline.readAll()).object();
        const auto offlineYaml = QByteArray::fromBase64(offlineArchive.value("profiles")
                                                            .toArray()
                                                            .first()
                                                            .toObject()
                                                            .value("yaml")
                                                            .toString()
                                                            .toLatin1());
        if (!check(offlineArchive.value("schemaVersion") == 4 &&
                       !offlineYaml.contains("local.example.com") &&
                       !offlineYaml.contains("DOMAIN-SUFFIX,example.com"),
                   "offline backup does not materialize user rules"))
            return 54;
        runtime.featureCommand("preview_backup", {offlineUrl});
        if (!check(!runtime.snapshot()
                        .value("features")
                        .toObject()
                        .value("backupPreview")
                        .toObject()
                        .value("token")
                        .toString()
                        .isEmpty(),
                   "offline rules backup can be previewed"))
            return 55;
        runtime.featureCommand("cancel_backup_restore", {});
        const auto currentPid = runtime.snapshot().value("corePid");
        runtime.featureCommand("preview_backup", {backupUrl});
        const auto preview =
            runtime.snapshot().value("features").toObject().value("backupPreview").toObject();
        if (!check(preview.value("hasGlobalRules") == true, "preview rules-aware backup"))
            return 23;
        runtime.featureCommand("restore_backup", {preview.value("token").toString(), "confirmed"});
        if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 4 &&
                       runtime.snapshot().value("corePid") == currentPid && selected() == idA &&
                       view.value("global").toArray() == changedGlobal,
                   "default restore keeps global rules and current session"))
            return 24;
        const auto restoredId =
            runtime.snapshot().value("profiles").toArray()[2].toObject().value("id").toString();
        runtime.selectProfile(restoredId);
        if (!check(done() && selected() == restoredId && context(restoredId) &&
                       view.value("local").toArray() == local && view.value("proxy") == "Alpha",
                   "restored profile gets its own remapped rules and binding"))
            return 25;
        rules = readRules();
        if (!check(rules.size() == 3 &&
                       rules[0].as<std::string>() == "DOMAIN,local.example.com,DIRECT" &&
                       rules[1].as<std::string>() == "DOMAIN-SUFFIX,example.com,DIRECT",
                   "restored rules are composed once"))
            return 26;
        if (!check(save("global", changedGlobal, "") && view.value("result") == "Saved",
                   "clear proxy binding while global rule is direct"))
            return 44;
        const auto beforeFailedRestore = manifest();
        runtime.featureCommand("preview_backup", {backupUrl});
        auto failedPreview =
            runtime.snapshot().value("features").toObject().value("backupPreview").toObject();
        runtime.featureCommand("restore_backup", {failedPreview.value("token").toString(),
                                                  "confirmed", "global_rules"});
        if (!check(
                done() && waitFor([&] {
                    return runtime.snapshot().value("features").toObject().value("backupResult") ==
                           "RulesFailed";
                }) &&
                    manifest() == beforeFailedRestore &&
                    runtime.snapshot().value("ready").toBool() &&
                    view.value("global").toArray() == changedGlobal,
                "failed global restore preserves confirmed rules and session"))
            return 45;
        if (!check(save("global", changedGlobal, "Alpha") && view.value("result") == "Saved",
                   "repair binding before global restore")) {
            std::fprintf(
                stderr, "view=%s\nruntime=%s\n",
                QJsonDocument(view).toJson(QJsonDocument::Compact).constData(),
                QJsonDocument(runtime.snapshot()).toJson(QJsonDocument::Compact).constData());
            return 46;
        }
        runtime.featureCommand("preview_backup", {backupUrl});
        const auto nextPreview =
            runtime.snapshot().value("features").toObject().value("backupPreview").toObject();
        runtime.featureCommand(
            "restore_backup", {nextPreview.value("token").toString(), "confirmed", "global_rules"});
        if (!check(
                done() && waitFor([&] {
                    return runtime.snapshot().value("features").toObject().value("backupResult") ==
                           "RulesRestored";
                }) &&
                    waitFor([&] { return view.value("global").toArray() == global; }),
                "explicit global restore uses apply transaction"))
            return 27;
        runtime.selectProfile(idA);
        if (!check(done() && selected() == idA && context(idA), "return to original A"))
            return 28;
        runtime.stop();
        if (!check(done(), "stop"))
            return 16;
    }
    {
        application::DesktopRuntime runtime(store);
        QJsonObject view;
        QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                         [&view](const application::RuntimeUpdate& update) {
                             if (update.values.contains("user_rules"))
                                 view = update.values.value("user_rules").toObject();
                         });
        runtime.initialize();
        runtime.setDemands({"user_rules"});
        if (!check(waitFor([&] { return view.value("profileId") == idA; }) &&
                       view.value("global").toArray().size() == 1 &&
                       view.value("local").toArray().size() == 1 && view.value("proxy") == "Alpha",
                   "restart preserves both scopes and binding"))
            return 17;
        runtime.deleteProfile(idA);
        if (!check(runtime.snapshot().value("selectedId").toString().isEmpty(), "delete selected"))
            return 18;
    }
    {
        application::DesktopRuntime runtime(store);
        QJsonObject view;
        QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                         [&view](const application::RuntimeUpdate& update) {
                             if (update.values.contains("user_rules"))
                                 view = update.values.value("user_rules").toObject();
                         });
        runtime.initialize();
        runtime.setDemands({"user_rules"});
        if (!check(waitFor([&] { return !view.value("revision").toString().isEmpty(); }) &&
                       view.value("global").toArray().size() == 1,
                   "global rules survive deletion of selected profile and restart"))
            return 19;
    }
    {
        QTemporaryDir subscriptionDirectory;
        infra::JsonStore subscriptionStore(subscriptionDirectory.path());
        if (!std::holds_alternative<std::monostate>(subscriptionStore.open()))
            return 29;
        application::DesktopRuntime runtime(subscriptionStore);
        QJsonObject view;
        QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                         [&view](const application::RuntimeUpdate& update) {
                             if (update.values.contains("user_rules"))
                                 view = update.values.value("user_rules").toObject();
                         });
        runtime.initialize();
        int mixed = port(), controller = port();
        while (mixed == controller)
            controller = port();
        runtime.setPorts(mixed, controller);
        TlsFixture origin;
        if (!origin.initialize())
            return 30;
        const auto source = [](const char* group) {
            return QStringLiteral("proxies: []\nproxy-groups:\n- {name: %1, type: select, "
                                  "proxies: [DIRECT]}\nrules:\n- MATCH,%1\n")
                .arg(QLatin1String(group))
                .toUtf8();
        };
        origin.subscription = source("One");
        runtime.addSubscription(
            QStringLiteral("https://127.0.0.1:%1/subscription").arg(origin.serverPort()),
            "Subscription");
        const auto done = [&] {
            return waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
        };
        if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 1,
                   "subscription imported"))
            return 31;
        const auto id = runtime.snapshot().value("selectedId").toString();
        runtime.setDemands({"user_rules"});
        if (!check(waitFor([&] { return view.value("profileId") == id && !id.isEmpty(); }),
                   "subscription rules context"))
            return 32;
        const QJsonArray rules{QJsonObject{{"id", "g"},
                                           {"type", "DOMAIN"},
                                           {"value", "example.com"},
                                           {"target", "PROXY"},
                                           {"enabled", true}}};
        runtime.featureCommand(
            "user_rules_save",
            {view.value("revision").toString(), "global", id,
             QString::fromUtf8(QJsonDocument(rules).toJson(QJsonDocument::Compact)), "One"});
        if (!check(done() && waitFor([&] { return view.value("result") == "Saved"; }),
                   "subscription binding saved"))
            return 33;
        runtime.start(false);
        if (!check(done() && runtime.snapshot().value("ready").toBool(), "subscription running"))
            return 34;
        const auto previousManifest = std::get<std::optional<QJsonObject>>(
                                          subscriptionStore.read("data/runtime/current.json"))
                                          .value();
        const auto pid = runtime.snapshot().value("corePid");
        origin.subscription = source("Two");
        runtime.updateProfile(id);
        if (!done())
            return 35;
        const auto binding =
            runtime.snapshot().value("features").toObject().value("userRuleBinding").toObject();
        if (!check(runtime.snapshot().value("corePid") == pid &&
                       std::get<std::optional<QJsonObject>>(
                           subscriptionStore.read("data/runtime/current.json"))
                               .value() == previousManifest &&
                       binding.value("groups").toArray().contains("Two"),
                   "subscription group removal preserves old session and requests repair"))
            return 35;
        runtime.featureCommand("user_rules_bind", {"Two"});
        if (!check(done() && runtime.snapshot().value("ready").toBool() &&
                       waitFor([&] { return view.value("proxy") == "Two"; }),
                   "subscription binding repair applies updated profile"))
            return 36;
        const auto readySnapshot = runtime.snapshot();
        int validations = 0;
        bool validating = false;
        const auto countedDone = [&] {
            return waitFor([&] {
                const auto current = runtime.snapshot();
                const bool active =
                    current.value("configLifecycle").toObject().value("validationActive").toBool();
                if (active && !validating)
                    ++validations;
                validating = active;
                return !current.value("busy").toBool();
            });
        };
        origin.subscription = source("Two") + "# comment-only revision\n";
        runtime.updateProfile(id, true);
        const bool commentApplied =
            countedDone() && runtime.snapshot().value("error").toString().isEmpty();
        if (!check(
                commentApplied && validations == 0 &&
                    runtime.snapshot().value("corePid") == readySnapshot.value("corePid") &&
                    runtime.snapshot().value("epoch") == readySnapshot.value("epoch"),
                "comment-only update with user rules reuses the running core without validation"))
            return 82;
        auto changed = source("Two");
        changed.replace("proxies: [DIRECT]", "proxies: [DIRECT, REJECT]");
        origin.subscription = changed;
        validations = 0;
        runtime.updateProfile(id, true);
        const bool changeApplied =
            countedDone() && runtime.snapshot().value("error").toString().isEmpty();
        if (!check(changeApplied && validations == 1 &&
                       runtime.snapshot().value("ready").toBool() &&
                       runtime.snapshot().value("epoch") != readySnapshot.value("epoch"),
                   "effective update with user rules validates once and applies"))
            return 83;
        runtime.stop();
    }
    {
        QTemporaryDir emptyDirectory;
        infra::JsonStore emptyStore(emptyDirectory.path());
        if (!std::holds_alternative<std::monostate>(emptyStore.open()))
            return 37;
        const QJsonArray global{QJsonObject{{"id", "no-profile"},
                                            {"type", "DOMAIN"},
                                            {"value", "example.com"},
                                            {"target", "PROXY"},
                                            {"enabled", true}}};
        {
            application::DesktopRuntime runtime(emptyStore);
            QJsonObject view;
            QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                             [&view](const application::RuntimeUpdate& update) {
                                 if (update.values.contains("user_rules"))
                                     view = update.values.value("user_rules").toObject();
                             });
            runtime.initialize();
            runtime.setDemands({"user_rules"});
            if (!check(waitFor([&] { return !view.value("revision").toString().isEmpty(); }),
                       "empty repository rule context"))
                return 38;
            runtime.featureCommand("backup", {"confirmed", "offline"});
            if (!check(runtime.snapshot().value("error") == "OperationUnavailable",
                       "empty repository without rules cannot create a backup"))
                return 69;
            runtime.featureCommand(
                "user_rules_save",
                {view.value("revision").toString(), "global", "",
                 QString::fromUtf8(QJsonDocument(global).toJson(QJsonDocument::Compact)), ""});
            if (!check(waitFor([&] { return view.value("result") == "Saved"; }),
                       "save global rules without selected profile"))
                return 39;
        }
        {
            application::DesktopRuntime runtime(emptyStore);
            QJsonObject view;
            QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                             [&view](const application::RuntimeUpdate& update) {
                                 if (update.values.contains("user_rules"))
                                     view = update.values.value("user_rules").toObject();
                             });
            runtime.initialize();
            runtime.setDemands({"user_rules"});
            if (!check(waitFor([&] { return view.value("global").toArray() == global; }),
                       "global rules without profile survive restart"))
                return 40;
            runtime.featureCommand("backup", {"confirmed", "offline"});
            const auto backupUrl =
                runtime.snapshot().value("features").toObject().value("backupUrl").toString();
            QFile backup(QUrl(backupUrl).toLocalFile());
            if (!check(backup.open(QIODevice::ReadOnly), "global-only backup created"))
                return 63;
            const auto archive = QJsonDocument::fromJson(backup.readAll()).object();
            if (!check(archive.value("schemaVersion") == 4 &&
                           archive.value("profiles").toArray().isEmpty() &&
                           archive.value("globalRules").toArray() == global,
                       "global-only archive contains rules without profiles"))
                return 64;
            auto changed = global;
            auto changedRule = changed.first().toObject();
            changedRule.insert("target", "DIRECT");
            changed[0] = changedRule;
            runtime.featureCommand(
                "user_rules_save",
                {view.value("revision").toString(), "global", "",
                 QString::fromUtf8(QJsonDocument(changed).toJson(QJsonDocument::Compact)), ""});
            if (!check(waitFor([&] { return view.value("global").toArray() == changed; }),
                       "change global-only rules before restore"))
                return 65;
            runtime.featureCommand("preview_backup", {backupUrl});
            auto preview =
                runtime.snapshot().value("features").toObject().value("backupPreview").toObject();
            if (!check(!preview.value("token").toString().isEmpty() &&
                           preview.value("hasGlobalRules") == true &&
                           preview.value("profiles").toArray().isEmpty(),
                       "global-only backup previews without profiles"))
                return 66;
            runtime.featureCommand("restore_backup",
                                   {preview.value("token").toString(), "confirmed"});
            if (!check(runtime.snapshot().value("features").toObject().value("backupResult") ==
                               "Restored" &&
                           view.value("global").toArray() == changed,
                       "default global-only restore leaves existing rules unchanged"))
                return 67;
            runtime.featureCommand("preview_backup", {backupUrl});
            preview =
                runtime.snapshot().value("features").toObject().value("backupPreview").toObject();
            runtime.featureCommand(
                "restore_backup", {preview.value("token").toString(), "confirmed", "global_rules"});
            if (!check(waitFor([&] {
                           return runtime.snapshot()
                                          .value("features")
                                          .toObject()
                                          .value("backupResult") == "RulesRestored" &&
                                  view.value("global").toArray() == global;
                       }) &&
                           runtime.snapshot().value("profiles").toArray().isEmpty(),
                       "explicit global-only restore saves rules without creating a profile"))
                return 68;
            const auto restoredPreferences = emptyStore.read("config/runtime_defaults.json");
            if (!check(std::holds_alternative<std::optional<QJsonObject>>(restoredPreferences) &&
                           std::get<std::optional<QJsonObject>>(restoredPreferences)
                                   .value_or(QJsonObject{})
                                   .value("userRules")
                                   .toObject()
                                   .value("global")
                                   .toArray() == global,
                       "global-only restore persists the rules"))
                return 70;
            runtime.featureCommand("user_rules_save", {"stale", "global", "", "[]", ""});
            if (!check(waitFor([&] { return view.value("result") == "Failed"; }),
                       "stale draft rejected"))
                return 41;
            const auto firstAttempt = view.value("attempt");
            runtime.featureCommand("user_rules_save", {"stale", "global", "", "[]", ""});
            if (!check(waitFor([&] { return view.value("attempt") != firstAttempt; }) &&
                           view.value("global").toArray() == global,
                       "repeated identical failures are published and keep saved rules"))
                return 42;
            const auto beforeReadOnly = view.value("attempt");
            emptyStore.freeze();
            runtime.featureCommand("user_rules_save",
                                   {view.value("revision").toString(), "global", "", "[]", ""});
            if (!check(waitFor([&] { return view.value("attempt") != beforeReadOnly; }) &&
                           runtime.snapshot().value("error") == "OperationUnavailable" &&
                           view.value("global").toArray() == global,
                       "read-only failure keeps previously saved rules"))
                return 43;
        }
    }
    {
        QTemporaryDir providerDirectory;
        infra::JsonStore providerStore(providerDirectory.path());
        if (!std::holds_alternative<std::monostate>(providerStore.open()))
            return 56;
        application::DesktopRuntime runtime(providerStore);
        QJsonObject view;
        QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                         [&view](const application::RuntimeUpdate& update) {
                             if (update.values.contains("user_rules"))
                                 view = update.values.value("user_rules").toObject();
                         });
        runtime.initialize();
        int mixed = port(), controller = port();
        while (mixed == controller)
            controller = port();
        runtime.setPorts(mixed, controller);
        TlsFixture origin;
        if (!origin.initialize())
            return 57;
        origin.payloads.insert("/rules", "payload:\n  - DOMAIN,one.invalid\n");
        origin.subscription =
            "proxies: []\nproxy-groups: [{name: A, type: select, proxies: [DIRECT]}]\n"
            "rule-providers:\n  domains:\n    type: http\n    behavior: classical\n    url: "
            "https://127.0.0.1:" +
            QByteArray::number(origin.serverPort()) +
            "/rules\n    interval: 3600\nrules:\n- RULE-SET,domains,DIRECT\n- MATCH,DIRECT\n";
        runtime.addSubscription(
            QStringLiteral("https://127.0.0.1:%1/subscription").arg(origin.serverPort()),
            "Rules provider");
        const auto done = [&] {
            return waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
        };
        if (!check(done() && !runtime.snapshot().value("selectedId").toString().isEmpty(),
                   "rule provider imported"))
            return 58;
        const auto id = runtime.snapshot().value("selectedId").toString();
        runtime.featureCommand("provider_mode", {id, "runtime"});
        if (!check(done() && runtime.snapshot().value("error").toString().isEmpty(),
                   "runtime provider mode"))
            return 59;
        runtime.setDemands({"user_rules"});
        if (!check(waitFor([&] { return view.value("providers").toArray().contains("domains"); }),
                   "existing runtime rule provider available in editor"))
            return 60;
        const QJsonArray local{QJsonObject{{"id", "set"},
                                           {"type", "RULE-SET"},
                                           {"value", "domains"},
                                           {"target", "DIRECT"},
                                           {"enabled", true}}};
        runtime.featureCommand(
            "user_rules_save",
            {view.value("revision").toString(), "local", id,
             QString::fromUtf8(QJsonDocument(local).toJson(QJsonDocument::Compact)), ""});
        if (!check(done() && waitFor([&] { return view.value("result") == "Saved"; }),
                   "profile RULE-SET validates and saves"))
            return 61;
        runtime.featureCommand("backup", {"confirmed", "offline"});
        if (!check(runtime.snapshot().value("error") == "UserRuleBackupUnavailable",
                   "offline backup rejects provider rule it cannot restore"))
            return 62;
    }
    {
        QTemporaryDir trafficDirectory;
        infra::JsonStore trafficStore(trafficDirectory.path());
        if (!std::holds_alternative<std::monostate>(trafficStore.open()))
            return 71;
        TlsFixture origin;
        if (!origin.initialize())
            return 72;
        ConnectFixture upstream(origin.serverPort());
        if (!upstream.listen(QHostAddress::LocalHost))
            return 73;
        QFile fixture(trafficDirectory.filePath("traffic.yaml"));
        if (!fixture.open(QIODevice::WriteOnly))
            return 74;
        fixture.write("proxies:\n- {name: node, type: http, server: 127.0.0.1, port: " +
                      QByteArray::number(upstream.serverPort()) +
                      "}\nproxy-groups:\n- {name: Route, type: select, proxies: [node]}\n"
                      "rules: ['MATCH,REJECT']\n");
        fixture.close();
        application::DesktopRuntime runtime(trafficStore);
        QJsonObject view;
        QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                         [&view](const application::RuntimeUpdate& update) {
                             if (update.values.contains("user_rules"))
                                 view = update.values.value("user_rules").toObject();
                         });
        runtime.initialize();
        int mixed = port(), controller = port();
        while (mixed == controller)
            controller = port();
        runtime.setPorts(mixed, controller);
        const auto done = [&] {
            return waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
        };
        runtime.importFile(QUrl::fromLocalFile(fixture.fileName()).toString(), "Traffic");
        runtime.setDemands({"user_rules"});
        if (!check(done() && !runtime.snapshot().value("selectedId").toString().isEmpty() &&
                       waitFor([&] {
                           return view.value("profileId") ==
                                      runtime.snapshot().value("selectedId") &&
                                  !view.value("revision").toString().isEmpty();
                       }),
                   "traffic fixture imported"))
            return 75;
        const auto save = [&](const QString& scope, const QJsonArray& rules) {
            const auto revision = view.value("revision").toString();
            runtime.featureCommand(
                "user_rules_save",
                {revision, scope, view.value("profileId").toString(),
                 QString::fromUtf8(QJsonDocument(rules).toJson(QJsonDocument::Compact)), "Route"});
            const bool saved = done() && waitFor([&] {
                                   return (view.value("revision") != revision &&
                                           view.value("result") == "Saved") ||
                                          view.value("result") == "Failed";
                               }) &&
                               view.value("result") == "Saved";
            if (!saved)
                std::fprintf(stderr, "Traffic rule save failed: %s\n",
                             qPrintable(runtime.snapshot().value("error").toString()));
            return saved;
        };
        QJsonArray global{QJsonObject{{"id", "traffic-global"},
                                      {"type", "IP-CIDR"},
                                      {"value", "127.0.0.1/32"},
                                      {"target", "PROXY"},
                                      {"enabled", true}}};
        if (!check(save("global", global), "save global traffic override"))
            return 76;
        runtime.start(false);
        if (!check(done() && runtime.snapshot().value("ready").toBool(), "traffic core running"))
            return 77;
        const auto request = [&](bool succeeds) {
            QNetworkAccessManager client;
            client.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"),
                                          static_cast<quint16>(mixed)));
            auto* response = client.get(QNetworkRequest(
                QUrl(QStringLiteral("https://127.0.0.1:%1/proof").arg(origin.serverPort()))));
            if (!waitFor([&] { return response->isFinished(); }))
                return false;
            if (!succeeds)
                return response->error() != QNetworkReply::NoError;
            return response->error() == QNetworkReply::NoError &&
                   response->readAll() == QByteArrayLiteral("quickclash-controlled-origin");
        };
        if (!check(request(true) && !upstream.targets.isEmpty(),
                   "global proxy rule forwards real traffic ahead of original reject"))
            return 78;
        QJsonArray local{QJsonObject{{"id", "traffic-local"},
                                     {"type", "IP-CIDR"},
                                     {"value", "127.0.0.1/32"},
                                     {"target", "REJECT"},
                                     {"enabled", true}}};
        if (!check(save("local", local), "save local traffic override"))
            return 79;
        const auto count = upstream.targets.size();
        if (!check(request(false) && upstream.targets.size() == count,
                   "local reject blocks traffic before global proxy rule"))
            return 80;
        auto disabled = local.first().toObject();
        disabled.insert("enabled", false);
        local[0] = disabled;
        if (!check(save("local", local) && request(true) && upstream.targets.size() > count,
                   "disabled local rule restores global proxy forwarding"))
            return 81;
        runtime.stop();
    }
    {
        QTemporaryDir failedDirectory;
        infra::JsonStore failedStore(failedDirectory.path());
        if (!std::holds_alternative<std::monostate>(failedStore.open()))
            return 47;
        QFile fixture(failedDirectory.filePath("profile.yaml"));
        if (!fixture.open(QIODevice::WriteOnly))
            return 48;
        fixture.write("proxies: []\nproxy-groups: [{name: A, type: select, proxies: [DIRECT]}]\n"
                      "rules: ['MATCH,DIRECT']\n");
        fixture.close();
        application::DesktopRuntime runtime(failedStore);
        QJsonObject view;
        QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                         [&view](const application::RuntimeUpdate& update) {
                             if (update.values.contains("user_rules"))
                                 view = update.values.value("user_rules").toObject();
                         });
        runtime.initialize();
        int mixed = port(), controller = port();
        while (mixed == controller)
            controller = port();
        runtime.setPorts(mixed, controller);
        runtime.importFile(QUrl::fromLocalFile(fixture.fileName()).toString(), "Failure fixture");
        const auto done = [&] {
            return waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
        };
        if (!check(done() && !runtime.snapshot().value("selectedId").toString().isEmpty(),
                   "failure fixture imported"))
            return 49;
        runtime.start(false);
        runtime.setDemands({"user_rules"});
        if (!check(done() && runtime.snapshot().value("ready").toBool() &&
                       waitFor([&] { return !view.value("revision").toString().isEmpty(); }),
                   "failure fixture running"))
            return 50;
        const auto before =
            std::get<std::optional<QJsonObject>>(failedStore.read("data/runtime/current.json"))
                .value();
        const auto pid = runtime.snapshot().value("corePid");
        const QJsonArray rules{QJsonObject{{"id", "new"},
                                           {"type", "DOMAIN"},
                                           {"value", "example.com"},
                                           {"target", "DIRECT"},
                                           {"enabled", true}}};
        runtime.featureCommand(
            "user_rules_save",
            {view.value("revision").toString(), "global", view.value("profileId").toString(),
             QString::fromUtf8(QJsonDocument(rules).toJson(QJsonDocument::Compact)), ""});
        if (!check(runtime.snapshot().value("busy").toBool(), "validation started"))
            return 51;
        failedStore.freeze();
        if (!check(done() && waitFor([&] { return view.value("result") == "Failed"; }) &&
                       std::get<std::optional<QJsonObject>>(
                           failedStore.read("data/runtime/current.json"))
                               .value() == before &&
                       runtime.snapshot().value("ready").toBool() &&
                       runtime.snapshot().value("corePid") == pid &&
                       view.value("global").toArray().isEmpty(),
                   "write failure during apply preserves confirmed manifest and session"))
            return 52;
        runtime.stop();
    }
    return 0;
}
} // namespace quickclash::test
