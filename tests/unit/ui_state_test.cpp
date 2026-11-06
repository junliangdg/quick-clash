#include "domain/proxy_latency.h"
#include "presentation/feature_registry.h"
#include "presentation/ui_state.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void ready(quickclash::presentation::UiState& state) {
    QElapsedTimer timer;
    timer.start();
    while (!state.settingsReady() && state.storageError().isEmpty() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    check(state.settingsReady(), "asynchronous preferences available");
}
} // namespace
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    using namespace quickclash::presentation;
    try {
        FeatureRegistry registry;
        {
            UiState rules;
            rules.setUiVisible(true);
            rules.setDemand("connection-rule", "connections", true);
            QJsonObject control{{"publicationGeneration", "1"},
                                {"epoch", "rule-source"},
                                {"ready", true},
                                {"selectedId", "profile-one"},
                                {"selectionGeneration", "7"}};
            rules.applyControl(control);
            const auto publish = [&](const QJsonObject& metadata) {
                rules.applyData({1,
                                 "rule-source",
                                 {{"connections", QJsonArray{QJsonObject{{"id", "connection"},
                                                                         {"epoch", "rule-source"},
                                                                         {"metadata", metadata}}}}},
                                 rules.dataDemands().generations});
            };
            publish({{"host", "WWW.Example.COM."}, {"destinationIP", "192.0.2.42"}});
            check(rules.beginConnectionRule("rule-source", "connection").isEmpty() &&
                      rules.currentPage() == "rules" &&
                      rules.connectionRule().value("type") == "DOMAIN" &&
                      rules.connectionRule().value("value") == "www.example.com" &&
                      rules.connectionRule().value("profileId") == "profile-one" &&
                      rules.userRulesDraft().isEmpty(),
                  "live connection suggests an exact domain for explicit editor review");
            rules.clearConnectionRule();
            publish({{"destinationIP", "192.0.2.42"}});
            check(rules.beginConnectionRule("rule-source", "connection").isEmpty() &&
                      rules.connectionRule().value("type") == "IP-CIDR" &&
                      rules.connectionRule().value("value") == "192.0.2.42/32",
                  "IP fallback covers only the selected IPv4 host");
            rules.clearConnectionRule();
            publish({{"destinationIP", "2001:db8::42"}});
            check(rules.beginConnectionRule("rule-source", "connection").isEmpty() &&
                      rules.connectionRule().value("type") == "IP-CIDR6" &&
                      rules.connectionRule().value("value") == "2001:db8::42/128",
                  "IPv6 fallback covers only the selected host");
            rules.clearConnectionRule();
            for (const auto& host : QStringList{"", "a,REJECT", "bad\nhost", "https://example.com",
                                                "*.example.com", "example.com:443", "a..example",
                                                "fe80::1%7", QString(254, QLatin1Char('a'))}) {
                publish({{"host", host}});
                check(!rules.beginConnectionRule("rule-source", "connection").isEmpty() &&
                          rules.connectionRule().isEmpty(),
                      "missing, malformed and rule-injection targets cannot create drafts");
            }
            publish({{"host", "example.com"}});
            const QVariantMap existing{{"baseRevision", "old"},
                                       {"form", QVariantMap{{"value", "keep.me"}}}};
            rules.setUserRulesDraft(existing);
            check(!rules.beginConnectionRule("rule-source", "connection").isEmpty() &&
                      rules.userRulesDraft() == existing && rules.connectionRule().isEmpty(),
                  "existing edits are retained without replacement");
            rules.setUserRulesDraft({});
            check(!rules.beginConnectionRule("old-epoch", "connection").isEmpty() &&
                      !rules.beginConnectionRule("rule-source", "ended").isEmpty(),
                  "stale epoch and ended connection cannot seed a rule");
            check(rules.beginConnectionRule("rule-source", "connection").isEmpty(),
                  "valid live source remains available after rejection");
            control.insert("selectedId", "profile-two");
            control.insert("selectionGeneration", "8");
            rules.applyControl(control);
            check(rules.connectionRule().isEmpty(),
                  "profile switch invalidates pending suggestion");
            control.insert("readOnly", true);
            rules.applyControl(control);
            check(!rules.beginConnectionRule("rule-source", "connection").isEmpty(),
                  "read-only state rejects suggestion before navigation");
            control.insert("readOnly", false);
            rules.applyControl(control);
            rules.applyData({1,
                             "rule-source",
                             {{"connection_status", QJsonObject{{"connectionsStale", true}}}},
                             rules.dataDemands().generations});
            check(!rules.beginConnectionRule("rule-source", "connection").isEmpty(),
                  "stale snapshot cannot seed a fresh rule");
        }
        if (app.arguments().contains(QStringLiteral("--connection-rule"))) {
            std::cout << "Connection rule identity, validation and draft retention passed\n";
            return 0;
        }
        {
            UiState proxies;
            const auto unicodeNode = QStringLiteral("中文节点测速记录长名称");
            const QJsonObject observed{{"state", "Success"}, {"delayMs", 42}};
            proxies.applyData(
                {1,
                 "proxy",
                 {{"delay_details", QJsonObject{{unicodeNode, QJsonObject{{"latest", observed}}}}},
                  {"delays", QJsonObject{{unicodeNode, "42 ms"}}}}});
            for (const auto& name :
                 QStringList{QString{}, QStringLiteral("中文"),
                             QStringLiteral("中文节点测速记录长名称更长"), unicodeNode}) {
                const auto result = proxies.proxyState(name);
                check(result.value("node") == name &&
                          result.value("result").toString() ==
                              (name == unicodeNode ? QStringLiteral("42 ms") : QString{}),
                      "startup without a current node never borrows a Unicode node's latency");
            }
            const QJsonObject unresolved{{"name", "unresolved"},
                                         {"type", "Selector"},
                                         {"members", QJsonArray{unicodeNode}},
                                         {"current", ""}};
            proxies.applyData({1, "proxy", {{"groups", QJsonArray{unresolved}}}});
            check(proxies.proxyState("unresolved").value("node").toString().isEmpty() &&
                      proxies.proxyState("unresolved").value("result").toString().isEmpty(),
                  "unconfirmed group selection has no latency during asynchronous startup");
            int observations = 0;
            const auto observe = [&] {
                ++observations;
                const auto state = proxies.proxyState("unresolved");
                check(state.value("node") == unicodeNode && state.value("result") == "81 ms",
                      "every synchronous observer sees matching groups and latency snapshot");
            };
            QObject::connect(&proxies, &UiState::groupsChanged, &proxies, observe);
            QObject::connect(&proxies, &UiState::nodesChanged, &proxies, observe);
            QObject::connect(&proxies, &UiState::delaysChanged, &proxies, observe);
            QObject::connect(&proxies, &UiState::proxyStatesChanged, &proxies, observe);
            auto confirmed = unresolved;
            confirmed.insert("current", unicodeNode);
            auto updated = observed;
            updated.insert("delayMs", 81);
            proxies.applyData({1,
                               "proxy",
                               {{"groups", QJsonArray{confirmed}},
                                {"nodes", QJsonArray{QJsonObject{{"name", unicodeNode}}}},
                                {"delays", QJsonObject{{unicodeNode, "81 ms"}}},
                                {"delay_details",
                                 QJsonObject{{unicodeNode, QJsonObject{{"latest", updated}}}}}}});
            check(observations == 4, "one completed snapshot publishes each proxy notification");
        }
        {
            UiState proxies;
            int stateNotifications = 0;
            quint64 signalledGroupRevision = 0;
            QVariantMap signalledGroup;
            QObject::connect(&proxies, &UiState::proxyStatesChanged, [&] { ++stateNotifications; });
            QObject::connect(&proxies, &UiState::groupsChanged, [&] {
                signalledGroupRevision = proxies.proxyGroupRevision();
                signalledGroup = proxies.proxyGroup("auto");
            });
            const auto groupTarget =
                quickclash::domain::ProxyLatency::targetHash("https://group.invalid/");
            const QJsonObject groupRecord{{"state", "Success"},
                                          {"delayMs", 42},
                                          {"source", "Core"},
                                          {"targetHash", groupTarget},
                                          {"time", "2026-11-01T00:00:00Z"}};
            const QJsonObject manual{{"state", "Success"},
                                     {"delayMs", 88},
                                     {"source", "Manual"},
                                     {"targetHash", "other"},
                                     {"time", "2026-11-01T00:01:00Z"}};
            QJsonArray groups{QJsonObject{{"name", "parent"},
                                          {"type", "Selector"},
                                          {"members", QJsonArray{"auto", "node"}},
                                          {"current", "auto"}},
                              QJsonObject{{"name", "auto"},
                                          {"type", "URLTest"},
                                          {"members", QJsonArray{"node"}},
                                          {"current", "node"},
                                          {"testTargetHash", groupTarget}},
                              QJsonObject{{"name", "balance"},
                                          {"type", "LoadBalance"},
                                          {"members", QJsonArray{"node"}},
                                          {"current", "node"}}};
            proxies.applyData(
                {1,
                 "proxy",
                 {{"groups", groups},
                  {"nodes", QJsonArray{QJsonObject{{"name", "node"}}}},
                  {"delay_details",
                   QJsonObject{
                       {"node", QJsonObject{{"latest", manual},
                                            {"records", QJsonArray{groupRecord, manual}}}}}}}});
            check(proxies.groupCount() == 3 && proxies.nodeCount() == 1 &&
                      proxies.proxyGroup("parent").value("members").toList().size() == 2 &&
                      proxies.proxyGroup("missing").isEmpty() &&
                      signalledGroupRevision == proxies.proxyGroupRevision() &&
                      signalledGroup.value("current") == "node",
                  "home reads counts and the requested group without a full-list projection");
            check(proxies.proxyState("parent").value("result") == "42 ms" &&
                      proxies.proxyState("auto").value("node") == "node" &&
                      proxies.proxyState("node", "auto").value("result") == "42 ms" &&
                      proxies.proxyState("node").value("result") == "88 ms",
                  "shared proxy view resolves nested groups and keeps test targets distinct");
            check(proxies.proxyState("auto").value("label").toString().contains("42 ms") &&
                      proxies.proxyState("balance").value("node").toString().isEmpty(),
                  "dropdown label shares latency while balancing does not invent a leaf");
            const auto completeProjection = proxies.proxyStates();
            check(completeProjection.size() == 4 &&
                      completeProjection.value("node").toMap() == proxies.proxyState("node") &&
                      completeProjection.value("auto").toMap() == proxies.proxyState("auto"),
                  "explicit full-state getter preserves its compatibility projection");
            const auto oldRevision = proxies.proxyStateRevision();
            const auto oldNotifications = stateNotifications;
            auto pending = groupRecord;
            pending.insert("source", "Manual");
            pending.insert("state", "Testing");
            auto twin = groups[1].toObject();
            twin.insert("name", "twin");
            groups.append(twin);
            proxies.applyData(
                {1,
                 "proxy",
                 {{"groups", groups},
                  {"delay_details",
                   QJsonObject{{"node", QJsonObject{{"latest", manual},
                                                    {"records", QJsonArray{pending, manual}}}}}}}});
            check(proxies.proxyState("node", "auto").value("result") == "Testing" &&
                      proxies.proxyState("node", "twin").value("result") == "Testing" &&
                      proxies.proxyState("parent").value("result") == "Testing" &&
                      proxies.proxyState("node").value("result") == "88 ms",
                  "all same-target group views share pending state without masking other targets");
            check(proxies.proxyStateRevision() == oldRevision + 1 &&
                      stateNotifications == oldNotifications + 1 &&
                      proxies.proxyStates().value("twin").toMap() == proxies.proxyState("twin"),
                  "a target-only change invalidates lazy bindings and the full projection");
            const auto unchangedRevision = proxies.proxyStateRevision();
            const auto unchangedGroupRevision = proxies.proxyGroupRevision();
            proxies.applyData(
                {1,
                 "proxy",
                 {{"groups", groups},
                  {"delay_details",
                   QJsonObject{{"node", QJsonObject{{"latest", manual},
                                                    {"records", QJsonArray{pending, manual}}}}}}}});
            check(proxies.proxyStateRevision() == unchangedRevision &&
                      proxies.proxyGroupRevision() == unchangedGroupRevision,
                  "an unchanged projection does not invalidate visible bindings");
            pending.insert("state", "Cancelled");
            proxies.applyData(
                {1,
                 "proxy",
                 {{"delay_details",
                   QJsonObject{{"node", QJsonObject{{"latest", manual},
                                                    {"records", QJsonArray{pending, manual}}}}}}}});
            check(proxies.proxyState("auto").value("result") == "Cancelled" &&
                      proxies.proxyState("node", "twin").value("result") == "Cancelled",
                  "cancelled target is shared by nested exits and repeated group members");
            const auto defaultUrl = QStringLiteral("https://default.invalid/");
            const auto defaultHash = quickclash::domain::ProxyLatency::targetHash(defaultUrl);
            QJsonObject defaultRecord{{"state", "Testing"},
                                      {"source", "Manual"},
                                      {"targetHash", defaultHash},
                                      {"time", "2026-11-01T00:02:00Z"}};
            auto plain = groups[0].toObject();
            plain.insert("name", "plain");
            plain.insert("members", QJsonArray{"node"});
            plain.insert("current", "node");
            groups.append(plain);
            proxies.applyControl(
                {{"publicationGeneration", "1"}, {"epoch", "proxy"}, {"delayTarget", defaultUrl}});
            const auto publishTargetRecords = [&] {
                const QJsonObject entry{{"latest", defaultRecord},
                                        {"records", QJsonArray{pending, defaultRecord}}};
                const QJsonObject details{{"node", entry}};
                proxies.applyData({1, "proxy", {{"groups", groups}, {"delay_details", details}}});
            };
            publishTargetRecords();
            check(proxies.proxyState("node", "parent").value("result") == "Testing" &&
                      proxies.proxyState("node", "plain").value("result") == "Testing" &&
                      proxies.proxyState("node", "auto").value("result") == "Cancelled",
                  "default-target groups share pending state without mixing group targets");
            defaultRecord.insert("state", "Success");
            defaultRecord.insert("delayMs", 56);
            publishTargetRecords();
            check(proxies.proxyState("node", "parent").value("result") == "56 ms" &&
                      proxies.proxyState("node", "plain").value("result") == "56 ms" &&
                      proxies.proxyState("node", "auto").value("result") == "Cancelled",
                  "completed default-target results synchronize across groups");
            const auto revisionBeforeTargetChange = proxies.proxyStateRevision();
            proxies.applyControl({{"publicationGeneration", "1"},
                                  {"epoch", "proxy"},
                                  {"delayTarget", "https://new-default.invalid/"}});
            check(proxies.proxyStateRevision() == revisionBeforeTargetChange + 1 &&
                      proxies.proxyState("node", "plain").value("result").toString().isEmpty() &&
                      proxies.proxyState("node", "auto").value("result") == "Cancelled",
                  "changing the default target invalidates group views without mixing targets");
            auto changed = groups[1].toObject();
            changed.insert("current", "absent");
            groups[1] = changed;
            const auto oldGroupRevision = proxies.proxyGroupRevision();
            const auto oldGroupCount = proxies.groupCount();
            proxies.applyData({1, "proxy", {{"groups", groups}}});
            check(proxies.proxyGroupRevision() == oldGroupRevision + 1 &&
                      proxies.groupCount() == oldGroupCount &&
                      signalledGroupRevision == proxies.proxyGroupRevision() &&
                      signalledGroup.value("current") == "absent",
                  "same-count group updates publish their new on-demand group before notification");
            check(proxies.proxyState("parent").value("node").toString().isEmpty(),
                  "missing current member is not presented as an exit node");
            changed.insert("current", "parent");
            changed.insert("members", QJsonArray{"parent"});
            groups[1] = changed;
            proxies.applyData({1, "proxy", {{"groups", groups}}});
            check(proxies.proxyState("parent").value("node").toString().isEmpty(),
                  "cyclic group chain terminates without fabricated latency");
            proxies.applyControl({{"publicationGeneration", "2"}, {"epoch", "new-proxy"}});
            check(proxies.delayDetails().isEmpty() && proxies.delays().isEmpty() &&
                      proxies.proxyState("node").value("result").toString().isEmpty(),
                  "new epoch clears latency before its replacement baseline arrives");
        }
        {
            UiState errors;
            for (const auto* code : {"CoreListenersNotOwned",
                                     "CoreListenerPortUnavailable",
                                     "CoreConfigReadbackFailed",
                                     "ServiceInstallationRequired",
                                     "ServiceClientUntrusted",
                                     "ServiceNotRunning",
                                     "ServiceSetupDeclined",
                                     "ServiceSetupClientRootRejected",
                                     "ServiceSetupPackageMissing",
                                     "ServiceSetupLaunchFailed",
                                     "ServiceSetupTimeout",
                                     "ServiceSetupFailed",
                                     "CoreListenerAddressInUse",
                                     "CoreListenerAccessDenied",
                                     "ServiceTunRouteConflict",
                                     "ServiceTunNameExhausted",
                                     "ServiceNetworkInspectionFailed",
                                     "NoAvailableListenerPorts",
                                     "CoreStopFailed",
                                     "CoreStopTimeout",
                                     "CoreModeReadbackFailed",
                                     "CoreModeRecoveryRequired"}) {
                errors.applyControl({{"publicationGeneration", "1"},
                                     {"epoch", "errors"},
                                     {"error", QString::fromLatin1(code)}});
                check(!errors.runtimeError().isEmpty() && errors.runtimeError() != code,
                      "startup and installation errors explain recovery instead of raw codes");
            }
            const auto download = errors.errorText(QStringLiteral("SubscriptionDownloadFailed"));
            check(!download.isEmpty() && download != QStringLiteral("SubscriptionDownloadFailed") &&
                      download != errors.runtimeError(),
                  "subscription error text uses its own error code instead of runtime state");
        }
        {
            UiState editor;
            editor.setUiVisible(true);
            editor.setDemand("edit", "editor", true);
            const quickclash::application::RuntimeUpdate original{
                1,
                "editor",
                {{"editor", QJsonObject{{"text", "private_draft"}, {"token", "first"}}}},
                editor.dataDemands().generations};
            editor.applyData(original);
            check(editor.editor().value("text") == "private_draft" &&
                      !editor.diagnosticSnapshot().contains("editor"),
                  "raw drafts have a private scoped channel outside diagnostics");
            bool beforeRelease = false;
            QObject::connect(&editor, &UiState::uiAboutToHide, [&] {
                beforeRelease = editor.uiVisible() && !editor.editor().isEmpty() &&
                                editor.dataDemands().active.contains("editor");
            });
            editor.setUiVisible(false);
            check(beforeRelease && editor.editor().isEmpty(),
                  "hide allows saving before revoking and clearing the editor lease");
            editor.setUiVisible(true);
            editor.setDemand("edit", "editor", true);
            editor.applyData(original);
            check(editor.editor().isEmpty(), "old draft publication cannot reappear after hide");
        }
        {
            qint64 now = 1000;
            UiState traffic({}, nullptr, [&now] { return now; });
            traffic.setUiVisible(true);
            traffic.applyControl(
                {{"publicationGeneration", "1"}, {"epoch", "traffic-window"}, {"ready", true}});
            const auto trafficGeneration = traffic.dataDemands().generations.value("traffic");
            check(traffic.dataDemands().active == QSet<QString>{"traffic"},
                  "visible host starts one traffic lease independently of the current page");
            traffic.setDemand("chart", "traffic", true);
            const auto deliver = [&traffic, &now](int second) {
                now = second * 1000LL;
                traffic.applyData(
                    {1,
                     "traffic-window",
                     {{"traffic",
                       QJsonObject{
                           {"traffic", QJsonObject{{"time", QString::number(second * 1000LL)},
                                                   {"up", second * 1024},
                                                   {"down", second * 2048}}}}}},
                     traffic.dataDemands().generations});
            };
            for (int second = 1; second <= 75; ++second)
                deliver(second);
            check(traffic.uploadHistory().size() == 60 && traffic.downloadHistory().size() == 60 &&
                      traffic.uploadHistory().constFirst().toDouble() == 16.0 &&
                      traffic.uploadHistory().constLast().toDouble() == 75.0 &&
                      traffic.downloadHistory().constLast().toDouble() == 150.0,
                  "60-second window drops the oldest sample and retains both series");
            const auto history = traffic.uploadHistory();
            deliver(75);
            check(traffic.uploadHistory() == history, "duplicate traffic sample is not appended");
            traffic.setDemand("chart", "traffic", false);
            traffic.navigate("settings");
            traffic.setThemeMode("dark");
            check(traffic.uploadHistory() == history,
                  "page and theme changes retain traffic history");
            check(traffic.dataDemands().generations.value("traffic") == trafficGeneration &&
                      traffic.dataDemands().active.contains("traffic") &&
                      traffic.upload() != QString::fromUtf8("—"),
                  "leaving the dashboard retains the stream generation and latest rate");
            deliver(76);
            check(traffic.uploadHistory().constLast().toDouble() == 76.0,
                  "traffic continues recording while another page is open");
            traffic.setDemand("chart", "traffic", true);
            deliver(77);
            check(traffic.uploadHistory().constFirst().toDouble() == 18.0,
                  "returning to the dashboard continues the same bounded window");
            traffic.setDemand("resources", "resources", true);
            traffic.setDemand("connections", "connections", true);
            traffic.setUiVisible(false, true);
            check(!traffic.uiVisible() && traffic.activeDemandCount() == 0 &&
                      traffic.dataDemands().active == QSet<QString>{"traffic"} &&
                      traffic.dataDemands().generations.value("traffic") == trafficGeneration,
                  "minimization releases page data and retains only the shared traffic stream");
            deliver(78);
            traffic.setUiVisible(true);
            check(traffic.uploadHistory().constLast().toDouble() == 78.0 &&
                      traffic.uploadHistory().at(58).toDouble() == 77.0 &&
                      traffic.dataDemands().generations.value("traffic") == trafficGeneration,
                  "restoring immediately exposes consecutive samples without a new lease");
            traffic.setUiVisible(false, true);
            const quickclash::application::RuntimeUpdate lateTraffic{
                1,
                "traffic-window",
                {{"traffic",
                  QJsonObject{{"traffic", QJsonObject{{"time", "79000"}, {"up", 1024}}}}}},
                traffic.dataDemands().generations};
            traffic.setUiVisible(false);
            check(traffic.uploadHistory().constLast().toDouble() == 78.0 &&
                      traffic.dataDemands().active == QSet<QString>{"traffic"} &&
                      traffic.dataDemands().generations.value("traffic") == trafficGeneration,
                  "hiding retains the ready core's shared stream and bounded history");
            traffic.applyData(lateTraffic);
            deliver(80);
            check(traffic.uploadHistory().at(58).toDouble() == 1.0 &&
                      traffic.uploadHistory().constLast().toDouble() == 80.0,
                  "hidden traffic continues to append without restarting the stream");
            traffic.setUiVisible(true);
            check(traffic.uploadHistory().constLast().toDouble() == 80.0,
                  "restoring from hidden immediately exposes recent samples");
            traffic.setDemand("chart", "traffic", true);
            deliver(100);
            deliver(104);
            const auto gaps = traffic.uploadHistory();
            check(gaps.size() == 60 && !gaps.at(58).isValid() && !gaps.at(57).isValid() &&
                      !gaps.at(56).isValid() && gaps.at(55).toDouble() == 100,
                  "missed seconds remain gaps rather than zero traffic");
            for (int second = 105; second < 165; ++second) {
                const auto rate = second % 3 == 0 ? 0 : second % 3 == 1 ? 1 : 1073741824;
                now = second * 1000LL + (second % 2 ? 40 : -40);
                traffic.applyData(
                    {1,
                     "traffic-window",
                     {{"traffic",
                       QJsonObject{{"traffic",
                                    QJsonObject{{"time", QString::number(second * 1000LL +
                                                                         (second % 2 ? 40 : -40))},
                                                {"up", rate},
                                                {"down", 1073741824 - rate}}}}}},
                     traffic.dataDemands().generations});
            }
            const auto peaks = traffic.uploadHistory();
            check(std::all_of(peaks.cbegin(), peaks.cend(),
                              [](const auto& point) { return point.isValid(); }) &&
                      peaks.at(57).toDouble() == 0 && peaks.at(58).toDouble() == 1.0 / 1024 &&
                      peaks.at(59).toDouble() == 1048576,
                  "zero, near-zero and extreme peaks remain consecutive with normal clock jitter");
            traffic.setUiVisible(false);
            const auto oldLease = traffic.dataDemands().generations;
            traffic.applyControl(
                {{"publicationGeneration", "1"}, {"epoch", "traffic-window"}, {"ready", false}});
            check(traffic.dataDemands().active.isEmpty() &&
                      traffic.uploadHistory().constLast().toDouble() == 1048576 &&
                      traffic.upload() == QString::fromUtf8("—"),
                  "an unconfirmed stop releases traffic but retains its last history");
            traffic.applyData({1,
                               "traffic-window",
                               {{"traffic", QJsonObject{{"traffic", QJsonObject{{"time", "165000"},
                                                                                {"up", 1024}}}}}},
                               oldLease});
            check(traffic.uploadHistory().constLast().toDouble() == 1048576,
                  "a late sample from the stopped stream cannot replace the history");
            traffic.applyControl(
                {{"publicationGeneration", "2"}, {"epoch", "new-core"}, {"ready", true}});
            traffic.applyData(lateTraffic);
            check(traffic.dataDemands().active == QSet<QString>{"traffic"} &&
                      traffic.dataDemands().generations.value("traffic") > trafficGeneration &&
                      traffic.uploadHistory().constLast().toDouble() == 1048576,
                  "new hidden session retains history and rejects the old epoch");
            traffic.setDashboardOption("resources", true);
            traffic.setDashboardOption("unknown", true);
            check(traffic.dashboard().value("resources").toBool() &&
                      !traffic.dashboard().contains("unknown"),
                  "bounded dashboard preferences");
        }
        {
            qint64 now = 1000;
            UiState background({}, nullptr, [&now] { return now; });
            check(background.dataDemands().active.isEmpty(),
                  "a hidden host without a ready core has no traffic demand");
            background.applyControl(
                {{"publicationGeneration", "1"}, {"epoch", "background-start"}, {"ready", true}});
            check(background.dataDemands().active == QSet<QString>{"traffic"},
                  "a core becoming ready before any window opens starts background traffic");
            background.applyData(
                {1,
                 "background-start",
                 {{"traffic",
                   QJsonObject{{"traffic", QJsonObject{{"time", "1000"}, {"up", 2048}}}}}},
                 background.dataDemands().generations});
            background.setUiVisible(true);
            check(background.uploadHistory().constLast().toDouble() == 2.0,
                  "the first window sees samples collected during hidden startup");
            background.setDemand("home", "traffic", true);
            const auto visibleLease = background.dataDemands().generations;
            background.applyControl(
                {{"publicationGeneration", "1"}, {"epoch", "background-start"}, {"ready", false}});
            background.applyData(
                {1,
                 "background-start",
                 {{"traffic",
                   QJsonObject{{"traffic", QJsonObject{{"time", "2000"}, {"up", 4096}}}}}},
                 visibleLease});
            check(!background.dataDemands().active.contains("traffic") &&
                      background.uploadHistory().constLast().toDouble() == 2.0,
                  "stopping a visible core revokes even the page traffic lease and rejects late "
                  "data");
            background.applyControl(
                {{"publicationGeneration", "2"}, {"epoch", "visible-restart"}, {"ready", true}});
            check(background.dataDemands().active.contains("traffic") &&
                      background.dataDemands().generations.value("traffic") >
                          visibleLease.value("traffic"),
                  "a ready visible restart creates a fresh traffic lease");
        }
        {
            qint64 now = 100000;
            UiState continuity({}, nullptr, [&now] { return now; });
            continuity.setUiVisible(true);
            const auto control = [&continuity](int generation, const QString& epoch, bool ready,
                                               const QString& status, bool busy = false) {
                continuity.applyControl({{"publicationGeneration", QString::number(generation)},
                                         {"epoch", epoch},
                                         {"ready", ready},
                                         {"status", status},
                                         {"busy", busy},
                                         {"recovering", false}});
            };
            const auto sample = [&continuity, &now](int generation, const QString& epoch, int rate,
                                                    int total) {
                continuity.applyData(
                    {static_cast<quint64>(generation),
                     epoch,
                     {{"traffic",
                       QJsonObject{{"traffic", QJsonObject{{"time", QString::number(now)},
                                                           {"up", rate * 1024},
                                                           {"down", rate * 2048},
                                                           {"upTotal", total}}}}}},
                     continuity.dataDemands().generations});
            };
            control(1, "first", true, "Ready");
            sample(1, "first", 8, 500);
            const auto oldLease = continuity.dataDemands().generations;
            now += 1000;
            control(1, "first", false, "Stopped");
            check(continuity.upload() == "0.0 KiB/s" && continuity.download() == "0.0 KiB/s" &&
                      continuity.uploadHistory().contains(8.0) &&
                      !continuity.telemetry().contains("traffic"),
                  "confirmed stop keeps prior history, clears session totals and shows zero rate");
            continuity.setUiVisible(false);
            now += 3000;
            auto points = continuity.uploadHistory();
            check(points.at(56).toDouble() == 8.0 && points.at(57).toDouble() == 0.0 &&
                      points.at(58).toDouble() == 0.0 && points.at(59).toDouble() == 0.0,
                  "confirmed stopped time fills zeros while the window is hidden");
            control(1, "first", false, "Stopped", true);
            check(continuity.upload() == QString::fromUtf8("—"),
                  "busy stopped state is not treated as a confirmed stop");
            now += 2000;
            points = continuity.uploadHistory();
            check(!points.at(59).isValid() && !points.at(58).isValid() &&
                      points.at(57).toDouble() == 0.0,
                  "uncertain time is blank rather than zero");
            control(1, "first", false, "Uncertain");
            now += 1000;
            control(2, "second", true, "Ready");
            continuity.applyData(
                {1,
                 "first",
                 {{"traffic", QJsonObject{{"traffic", QJsonObject{{"time", QString::number(now)},
                                                                  {"up", 999 * 1024}}}}}},
                 oldLease});
            check(!continuity.telemetry().contains("traffic") &&
                      continuity.upload() == QString::fromUtf8("—"),
                  "old epoch cannot restore its rate or cumulative total");
            sample(2, "second", 12, 7);
            points = continuity.uploadHistory();
            check(points.contains(8.0) && points.constLast().toDouble() == 12 &&
                      !points.at(58).isValid() &&
                      continuity.telemetry().value("traffic").toMap().value("upTotal").toInt() == 7,
                  "new session follows retained history across a gap with its own total");
            now += 4000;
            points = continuity.uploadHistory();
            check(!points.constLast().isValid() && points.contains(12.0),
                  "a missing live stream advances as gaps without a later sample");
            continuity.setUiVisible(true);
            now += 1000;
            control(2, "second", false, "Stopped");
            continuity.setUiVisible(false);
            now += 61000;
            continuity.setUiVisible(true);
            points = continuity.uploadHistory();
            check(points.size() == 60 && std::all_of(points.cbegin(), points.cend(),
                                                     [](const auto& point) {
                                                         return point.isValid() &&
                                                                point.toDouble() == 0;
                                                     }),
                  "long hidden stop naturally replaces the complete window with zeros");
        }
        {
            qint64 now = 2;
            UiState live({}, nullptr, [&now] { return now; });
            live.setUiVisible(true);
            check(live.createDemandKey("rules") != live.createDemandKey("rules"),
                  "page instances own distinct keys so old destruction cannot revoke a new page");
            for (const auto* stream : {"traffic", "connections", "logs"})
                live.setDemand(QLatin1String(stream), QLatin1String(stream), true);
            int controlChanges = 0, profileChanges = 0, groupChanges = 0, samples = 0;
            QObject::connect(&live, &UiState::runtimeChanged, [&] { ++controlChanges; });
            QObject::connect(&live, &UiState::profilesChanged, [&] { ++profileChanges; });
            QObject::connect(&live, &UiState::groupsChanged, [&] { ++groupChanges; });
            QObject::connect(&live, &UiState::sampleChanged, [&] { ++samples; });
            const QJsonObject control{
                {"publicationGeneration", "1"}, {"epoch", "a"}, {"status", "Ready"}};
            live.applyControl(control);
            live.applyControl(control);
            check(controlChanges == 1, "unchanged compact state has no notification");
            const quickclash::application::RuntimeUpdate initial{
                1,
                "a",
                {{"profiles", QJsonArray{QJsonObject{{"id", "p"}}}},
                 {"groups", QJsonArray{QJsonObject{{"name", "g"}}}},
                 {"traffic",
                  QJsonObject{
                      {"traffic", QJsonObject{{"time", "1"}, {"up", 1024}, {"down", 2048}}}}},
                 {"connections", QJsonArray{QJsonObject{{"id", "c"}, {"epoch", "a"}}}},
                 {"logs", QJsonArray{QJsonObject{{"localSeq", "1"}, {"message", "first"}}}}},
                live.dataDemands().generations};
            live.applyData(initial);
            live.applyData(initial);
            check(profileChanges == 1 && groupChanges == 1 && samples == 1,
                  "identical channels do not reconvert or notify");
            live.applyData(
                {1,
                 "a",
                 {{"samples", QJsonObject{{"coreUptimeSeconds", 2}}},
                  {"traffic", QJsonObject{{"traffic", QJsonObject{{"time", "2"}, {"up", 2048}}}}}},
                 live.dataDemands().generations});
            check(profileChanges == 1 && groupChanges == 1 && controlChanges == 1 && samples == 2,
                  "telemetry does not notify unrelated control/profile/group consumers");
            check(!live.runtime().contains("connections") && !live.runtime().contains("profiles"),
                  "production runtime excludes full collections");
            check(live.diagnosticSnapshot().value("profiles").toArray().size() == 1,
                  "explicit diagnostic compatibility retains collections");
            live.setLogsPaused(true);
            live.applyData(
                {1,
                 "a",
                 {{"logs", QJsonArray{QJsonObject{{"localSeq", "2"}, {"message", "second"}}}}},
                 live.dataDemands().generations});
            check(live.liveLogs()->index(0, 0).data(RuntimeRows::Message) == "first",
                  "pause freezes GUI rows");
            live.setLogsPaused(false);
            check(live.liveLogs()->index(0, 0).data(RuntimeRows::Message) == "second",
                  "resume applies latest bounded log state");
            live.applyData(
                {2,
                 "b",
                 {{"connections", QJsonArray{QJsonObject{{"id", "new"}, {"epoch", "b"}}}}},
                 live.dataDemands().generations});
            live.applyData(initial);
            live.applyControl(control);
            check(live.liveConnections()->captureIds() == QStringList{"new"},
                  "old generation cannot revive old connections");
            check(live.uploadHistory().size() == 60 &&
                      live.uploadHistory().constLast().toDouble() == 2,
                  "epoch preserves prior traffic history");
            check(profileChanges == 1 && groupChanges == 1,
                  "epoch alone does not notify unrelated profiles/groups");
        }
        {
            UiState live;
            live.setUiVisible(true);
            live.setDemand("one", "connections", true);
            live.setDemand("two", "connections", true);
            const auto first = live.dataDemands().generations;
            const quickclash::application::RuntimeUpdate old{
                1,
                "same",
                {{"connections", QJsonArray{QJsonObject{{"id", "one"}, {"epoch", "same"}}}},
                 {"connection_status", QJsonObject{{"connectionsStale", false}}}},
                first};
            live.applyData(old);
            live.setDemand("one", "connections", false);
            check(live.liveConnections()->count() == 1 && live.dataDemands().generations == first,
                  "another consumer retains the cache and its lease");
            live.setDemand("two", "connections", false);
            check(live.liveConnections()->count() == 0 &&
                      !live.diagnosticSnapshot().contains("connections") &&
                      !live.telemetry().contains("connectionsStale"),
                  "last release clears model, JSON baseline and associated status");
            live.applyData(old);
            check(live.liveConnections()->count() == 0,
                  "late mailbox data cannot refill a released view");
            live.setDemand("again", "connections", true);
            live.applyData(old);
            check(live.liveConnections()->count() == 0,
                  "same-session reentry rejects previous lease");
            auto fresh = old;
            fresh.demandGenerations = live.dataDemands().generations;
            live.applyData(fresh);
            check(live.liveConnections()->count() == 1,
                  "unchanged full value is accepted after reentry");
            live.setDemand("again", "rules", true);
            check(live.liveConnections()->count() == 0,
                  "changing a token's stream releases its old stream");
            for (const auto* stream : {"rules", "providers", "logs", "traffic", "resources"})
                live.setDemand(QLatin1String(stream), QLatin1String(stream), true);
            const auto resourceGeneration = live.dataDemands().generations.value("resources");
            live.setDemand("resource_second_view", "resources", true);
            live.setDemand("resources", "resources", false);
            check(live.dataDemands().active.contains("resources") &&
                      live.dataDemands().generations.value("resources") == resourceGeneration,
                  "multiple resource views share the same sampling lease until last release");
            const auto revisions = live.dataDemands().generations;
            const quickclash::application::RuntimeUpdate populated{
                1,
                "same",
                {{"rules", QJsonArray{QJsonObject{{"type", "MATCH"}}}},
                 {"providers", QJsonArray{QJsonObject{{"name", "p"}, {"kind", "rules"}}}},
                 {"logs", QJsonArray{QJsonObject{{"localSeq", "1"}, {"message", "frozen"}}}},
                 {"traffic", QJsonObject{{"traffic", QJsonObject{{"time", "1"}, {"up", 1}}}}},
                 {"resources", QJsonObject{{"memory", QJsonObject{{"client", 1}}}}}},
                revisions};
            live.applyData(populated);
            live.setLogsPaused(true);
            live.applyData(
                {1,
                 "same",
                 {{"logs", QJsonArray{QJsonObject{{"localSeq", "2"}, {"message", "pending"}}}}},
                 revisions});
            check(live.liveLogs()->index(0, 0).data(RuntimeRows::Message) == "frozen",
                  "visible pause retains its bounded frozen snapshot");
            live.setUiVisible(false);
            const auto diagnostic = live.diagnosticSnapshot();
            check(live.liveRules()->count() == 0 && live.liveProviders()->count() == 0 &&
                      live.liveLogs()->count() == 0 && live.liveConnections()->count() == 0 &&
                      live.uploadHistory().size() == 60 && !live.telemetry().contains("memory"),
                  "hide releases page models, paused logs and resources while keeping a bounded "
                  "traffic ring");
            const auto lifecycle = diagnostic.value("dataLifecycle").toObject();
            check(lifecycle.value("appliedChannels").toArray().isEmpty() &&
                      lifecycle.value("pendingLogCount").toInt() == 0,
                  "hide clears every GUI channel baseline and pending log copy");
            live.applyData(populated);
            live.setLogsPaused(false);
            check(live.liveLogs()->count() == 0,
                  "resume after leaving cannot restore a stale snapshot");
            live.setUiVisible(true);
            live.setDemand("logs", "logs", true);
            fresh = {1,
                     "same",
                     {{"logs", QJsonArray{QJsonObject{{"localSeq", "3"}, {"message", "fresh"}}}}},
                     live.dataDemands().generations};
            live.applyData(fresh);
            live.runtimeCommand("clear_logs");
            live.applyData(fresh);
            check(live.liveLogs()->count() == 0,
                  "clear fences already queued logs even while paused");
        }
        check(registry.features().size() == 8, "eight independent modules");
        check(registry.project({"home", "settings", "home", "unknown"}).size() == 2,
              "mobile selection deduplicates and does not affect routing");
        check(registry.resolve("connections") != nullptr, "non-projected module remains routable");
        check(!registry.resolve("file:///tmp/evil.qml"), "caller cannot supply a component URL");
        check(!registry.resolve("home", {{"secret", "value"}}), "unexpected context rejected");
        check(!registry.resolve("connections", {{"filter", QVariantMap{}}}),
              "nested context rejected");
        check(!registry.resolve("connections", {{"filter", QString(129, QLatin1Char('x'))}}),
              "bounded context");
        check(!registry.resolve("proxies", {{"group", "unknown"}}),
              "unknown group context rejected");
        check(!registry.resolveEmbedded("proxy_summary", {{"source", "file:///evil"}}),
              "embedded context validated");
        QTemporaryDir temporary;
        check(temporary.isValid(), "isolated settings directory");
        const auto settings = temporary.filePath("ui.ini");
        {
            UiState state(settings);
            ready(state);
            const auto compact = state.compactNavigation();
            check(compact.size() == 4 && compact[0].toMap().value("pageId") == "home" &&
                      compact[1].toMap().value("pageId") == "proxies" &&
                      compact[2].toMap().value("pageId") == "subscriptions" &&
                      compact[3].toMap().value("pageId") == "settings",
                  "four compact destinations");
            check(state.navigate("connections") && state.navigation().size() == 8,
                  "compact projection keeps secondary routes available");
            check(state.navigate("home"), "return to dashboard");
            check(!state.replay() && !state.uiVisible(), "safe initial state");
            state.setSystemDark(true);
            check(state.dark(), "system theme fallback");
            state.setThemeMode("light");
            ready(state);
            state.setSystemDark(true);
            check(!state.dark(), "explicit preference wins");
            state.setLanguageMode("zh");
            ready(state);
            check(state.languageMode() == "zh", "language preference applies");
            state.setLanguageMode("fr");
            check(state.languageMode() == "zh", "unsupported language is ignored");
            check(!state.navigate("unknown") && state.currentPage() == "home",
                  "bad route keeps current page");
            state.setReplay(true);
            check(state.proxyModel()->count() == 500 && state.connectionModel()->count() == 1000,
                  "representative supported replay scale");
            check(!state.setReplayConnections(999) && state.replayConnections() == 1000,
                  "invalid scale retains previous model");
            check(state.setReplayConnections(10000) && state.connectionModel()->count() == 10000,
                  "explicit exploratory scale");
            state.setReplay(false);
            check(state.connectionModel()->count() == 0, "disabled replay releases rows");
            check(state.setReplayConnections(1000), "restore representative scale");
            state.setReplay(true);
            check(state.uploadHistory().size() == 60 && state.downloadHistory().size() == 60,
                  "bounded representative traffic curves");
            state.setDemand("hidden", "traffic", true);
            check(state.activeDemandCount() == 0, "hidden cannot register demand");
            state.setUiVisible(true);
            state.setDemand("home:proxy", "proxies", true);
            state.setDemand("page:proxy", "proxies", true);
            check(state.activeDemandCount() == 2 && state.activeStreamCount() == 1,
                  "shared stream merges consumers");
            check(!state.replayRunning(), "static proxy list has no periodic timer");
            state.setDemand("home:traffic", "traffic", true);
            check(state.replayRunning(), "visible replay starts timer on demand");
            state.setUiVisible(false);
            check(state.activeDemandCount() == 0 && !state.replayRunning(),
                  "hide releases all demands");
            state.setUiVisible(true);
            check(state.activeDemandCount() == 0, "old tokens do not revive on show");
            state.saveState("connections", {{"filter", "app_2"}, {"position", 212.0}});
            check(state.readState("home").isEmpty(), "instance state isolation");
            state.setFilter("connections", "app_2");
            check(state.connectionModel()->count() > 0 && state.connectionModel()->count() < 10000,
                  "filter works");
            check(!state.connectionModel()
                       ->data(state.connectionModel()->index(20000), ReplayModel::Name)
                       .isValid(),
                  "invalid index safe");
            for (int cycle = 0; cycle < 100; ++cycle) {
                state.setDemand("connections:list", "connections", true);
                state.setUiVisible(false);
                state.setUiVisible(true);
            }
            check(state.activeDemandCount() == 0 && !state.replayRunning(),
                  "repeated lifecycle leaves no demand");
        }
        UiState reopened(settings);
        ready(reopened);
        check(reopened.themeMode() == "light", "theme persists across process facade lifetime");
        check(reopened.languageMode() == "zh", "language persists across process facade lifetime");
        std::cout << "UI state contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
