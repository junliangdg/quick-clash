#include "presentation/feature_registry.h"
#include "presentation/ui_state.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
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
            UiState traffic;
            traffic.setUiVisible(true);
            traffic.setDemand("chart", "traffic", true);
            const auto deliver = [&traffic](int second) {
                traffic.applyData(
                    {1,
                     "traffic-window",
                     {{"traffic",
                       QJsonObject{{"traffic", QJsonObject{{"time", QString::number(second)},
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
            traffic.setDemand("chart", "traffic", true);
            deliver(76);
            check(traffic.uploadHistory().constFirst().toDouble() == 17.0,
                  "reacquiring traffic continues the same bounded window");
            traffic.setUiVisible(false);
            check(traffic.uploadHistory().size() == 60 &&
                      traffic.uploadHistory().constLast().toDouble() == 0.0,
                  "hide resets the 60-point window");
        }
        {
            UiState live;
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
                      live.uploadHistory().constFirst().toDouble() == 0.0,
                  "epoch resets traffic history to a full zero ring");
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
                  "hide releases all page models, paused logs and resources; traffic ring resets "
                  "to zeros");
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
        std::cout << "UI state contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
