#include "presentation/feature_registry.h"
#include "presentation/ui_state.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
} // namespace
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    using namespace quickclash::presentation;
    try {
        FeatureRegistry registry;
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
            check(!state.replay() && !state.uiVisible(), "safe initial state");
            state.setSystemDark(true);
            check(state.dark(), "system theme fallback");
            state.setThemeMode("light");
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
            check(state.uploadHistory().size() == 120 && state.downloadHistory().size() == 120,
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
        check(reopened.themeMode() == "light", "theme persists across process facade lifetime");
        std::cout << "UI state contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
