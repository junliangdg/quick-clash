#include "runtime_ui_probe.h"
#include "application_host.h"
#include <QElapsedTimer>
#include <QEvent>
#include <QFontInfo>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QSet>
#include <QTest>
#include <algorithm>
#include <cstdio>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

namespace quickclash {
namespace {
QJsonObject entry(const QString& id, const QString& host) {
    return {
        {"id", id}, {"epoch", "fixture"}, {"metadata", QJsonObject{{"host", host}}}, {"start", id}};
}
bool click(ApplicationHost& host, const QString& name) {
    auto* item = host.window()->findChild<QQuickItem*>(name);
    if (!item || !item->isEnabled() || !item->isVisible())
        return false;
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    QTest::mouseClick(host.window(), Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(20);
    return true;
}
} // namespace
int runRuntimeListProbe(ApplicationHost& host) {
    // No SettingsService, core, network or platform actions: only synthetic GUI channel delivery.
    auto& ui = host.state();
    ui.setReplay(false);
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"status", "Ready"}});
    if (!ui.navigate("connections"))
        return 40;
    QTest::qWait(60);
    const application::RuntimeUpdate oldConnections{
        1,
        "fixture",
        {{"connections", QJsonArray{entry("one", "alpha"), entry("two", "beta")}}},
        ui.dataDemands().generations};
    ui.applyData(oldConnections);
    auto* filter = host.window()->findChild<QObject*>(QStringLiteral("runtime_filter"));
    if (!filter || !filter->setProperty("text", "alpha"))
        return 41;
    QTest::qWait(60);
    if (ui.liveConnections()->count() != 2)
        return 42;
    QTest::qWait(140);
    if (ui.liveConnections()->captureIds() != QStringList{"one"})
        return 43;
    if (!click(host, QStringLiteral("close_filtered_connections")))
        return 44;
    auto* page = host.window()->findChild<QObject*>(QStringLiteral("connections_page"));
    auto* dialog =
        host.window()->findChild<QObject*>(QStringLiteral("filtered_connections_confirmation"));
    if (!page || !dialog || !dialog->property("visible").toBool() ||
        page->property("closeEpoch") != "fixture")
        return 45;
    ui.applyData({1,
                  "fixture",
                  {{"connections", QJsonArray{entry("one", "alpha"), entry("new", "alpha")}}},
                  ui.dataDemands().generations});
    if (page->property("closeIds").toStringList() != QStringList{"one"})
        return 46;
    QMetaObject::invokeMethod(dialog, "reject");
    QTest::qWait(150);
    if (!QMetaObject::invokeMethod(page, "showDetails",
                                   Q_ARG(QVariant, entry("one", "alpha").toVariantMap())))
        return 76;
    auto* details = host.window()->findChild<QObject*>(QStringLiteral("connection_details"));
    if (!details || !details->property("visible").toBool() || page->property("selectedId") != "one")
        return 77;
    filter->setProperty("text", "new");
    ui.applyData({1,
                  "fixture",
                  {{"connections", QJsonArray{entry("one", "changed"), entry("new", "alpha")}}},
                  ui.dataDemands().generations});
    QTest::qWait(180);
    auto selected = page->property("selectedEntry");
    if (selected.metaType() == QMetaType::fromType<QJSValue>())
        selected = selected.value<QJSValue>().toVariant();
    if (page->property("selectedEnded").toBool() ||
        selected.toMap().value("metadata").toMap().value("host") != "changed")
        return 78;
    ui.applyData({1,
                  "fixture",
                  {{"connections", QJsonArray{entry("new", "alpha")}}},
                  ui.dataDemands().generations});
    if (!page->property("selectedEnded").toBool() || page->property("selectedId") != "one")
        return 79;
    QMetaObject::invokeMethod(details, "reject");
    QTest::qWait(150);
    ui.applyData(
        {1,
         "fixture",
         {{"connection_status", QJsonObject{{"connectionsPaused", true},
                                            {"connectionsStale", true},
                                            {"connectionSample", "2026-10-28T00:00:00Z"}}}},
         ui.dataDemands().generations});
    QTest::qWait(20);
    auto* status = host.window()->findChild<QObject*>(QStringLiteral("connection_status"));
    if (!status ||
        status->property("text").toString() !=
            QCoreApplication::translate(
                "connections_page", "Connection data is too large or invalid; updates paused") ||
        ui.liveConnections()->count() != 1)
        return 80;
    if (!ui.navigate("rules"))
        return 47;
    QTest::qWait(60);
    if (ui.liveConnections()->count() != 0 || !ui.dataDemands().active.contains("rules"))
        return 59;
    const application::RuntimeUpdate oldRules{
        1,
        "fixture",
        {{"rules", QJsonArray{QJsonObject{{"type", "DOMAIN"}, {"payload", "alpha"}},
                              QJsonObject{{"type", "MATCH"}}}}},
        ui.dataDemands().generations};
    ui.applyData(oldRules);
    auto* section = host.window()->findChild<QObject*>(QStringLiteral("rules_section"));
    auto* view = host.window()->findChild<QObject*>(QStringLiteral("runtime_view"));
    if (!section || !view || view->property("count").toInt() != 2)
        return 48;
    auto* rulesPage = host.window()->findChild<QObject*>(QStringLiteral("rules_page"));
    QString description;
    const QVariant ruleDescription = QVariantMap{{"position", 7},
                                                 {"type", "RuleSet"},
                                                 {"payload", "fixture"},
                                                 {"providerName", "fixture"},
                                                 {"proxy", "DIRECT"}};
    if (!rulesPage ||
        !QMetaObject::invokeMethod(rulesPage, "describeEntry", Q_RETURN_ARG(QString, description),
                                   Q_ARG(QVariant, ruleDescription)) ||
        !description.contains("#7") ||
        !description.contains(QCoreApplication::translate("rules_page", "Unknown")) ||
        !description.contains(QCoreApplication::translate("rules_page", "Limited information")))
        return 83;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"selectedId", "selected-fixture"},
                     {"status", "Ready"}});
    if (!click(host, QStringLiteral("edit_rules")))
        return 84;
    auto* rulesEditor = host.window()->findChild<QObject*>(QStringLiteral("profile_editor"));
    if (!rulesEditor || !rulesEditor->property("visible").toBool() ||
        rulesEditor->property("profileId") != "selected-fixture" ||
        rulesEditor->property("kind") != "overlay" || !ui.dataDemands().active.contains("editor"))
        return 85;
    QMetaObject::invokeMethod(rulesEditor, "close");
    QTest::qWait(150);
    if (ui.dataDemands().active.contains("editor"))
        return 86;
    ui.applyData({1,
                  "fixture",
                  {{"rule_status", QJsonObject{{"rulePage", QJsonObject{{"loading", true}}}}}},
                  ui.dataDemands().generations});
    auto* refreshRules = host.window()->findChild<QObject*>(QStringLiteral("refresh_rules"));
    if (!refreshRules || refreshRules->property("enabled").toBool())
        return 81;
    section->setProperty("currentIndex", 1);
    QTest::qWait(30);
    if (ui.liveRules()->count() != 0 || ui.dataDemands().active.contains("rules") ||
        !ui.dataDemands().active.contains("providers") || ui.telemetry().contains("rulePage"))
        return 60;
    ui.applyData(oldRules);
    const application::RuntimeUpdate oldProviders{
        1,
        "fixture",
        {{"providers",
          QJsonArray{QJsonObject{{"name", "fixture"}, {"kind", "rules"}, {"count", 2}}}}},
        ui.dataDemands().generations};
    ui.applyData(oldProviders);
    if (view->property("count").toInt() != 1)
        return 49;
    ui.applyData(
        {1,
         "fixture",
         {{"provider_status",
           QJsonObject{{"providerPage",
                        QJsonObject{{"stale", true}, {"proxyError", "CoreRequestTimeout"}}}}}},
         ui.dataDemands().generations});
    auto* ruleStatus = host.window()->findChild<QObject*>(QStringLiteral("rules_status"));
    if (!ruleStatus || !ruleStatus->property("text").toString().contains("CoreRequestTimeout") ||
        ui.liveProviders()->count() != 1)
        return 82;
    ui.setUiVisible(false);
    ui.applyData(oldProviders);
    if (ui.liveProviders()->count() != 0 || ui.activeDemandCount() != 0)
        return 61;
    ui.setUiVisible(true);
    QTest::qWait(30);
    ui.applyData(oldProviders);
    if (ui.liveProviders()->count() != 0)
        return 62;
    auto currentProviders = oldProviders;
    currentProviders.demandGenerations = ui.dataDemands().generations;
    ui.applyData(currentProviders);
    if (ui.liveProviders()->count() != 1)
        return 63;
    section->setProperty("currentIndex", 0);
    QTest::qWait(30);
    ui.applyData(oldRules);
    ui.applyData(oldProviders);
    if (ui.liveRules()->count() != 0 || ui.liveProviders()->count() != 0 ||
        ui.dataDemands().active.contains("providers"))
        return 70;
    auto currentRules = oldRules;
    currentRules.demandGenerations = ui.dataDemands().generations;
    ui.applyData(currentRules);
    if (ui.liveRules()->count() != 2)
        return 71;
    QJsonArray logs;
    for (int i = 1; i <= 100; ++i)
        logs.append(QJsonObject{{"localSeq", QString::number(i)},
                                {"time", "now"},
                                {"source", "Core"},
                                {"level", "info"},
                                {"message", "fixture"}});
    if (!ui.navigate("logs"))
        return 50;
    QTest::qWait(60);
    if (ui.liveProviders()->count() != 0 || ui.dataDemands().active.contains("providers") ||
        ui.liveRules()->count() != 0 || ui.dataDemands().active.contains("rules"))
        return 64;
    ui.applyData({1, "fixture", {{"logs", logs}}, ui.dataDemands().generations});
    QTest::qWait(30);
    view = host.window()->findChild<QObject*>(QStringLiteral("runtime_view"));
    if (!view || !view->property("atYEnd").toBool())
        return 51;
    if (!click(host, QStringLiteral("pause_logs")))
        return 52;
    if (!ui.dataDemands().active.contains("logs") || ui.dataDemands().active.contains("log_feed"))
        return 65;
    logs.append(QJsonObject{
        {"localSeq", "101"}, {"source", "Core"}, {"level", "info"}, {"message", "later"}});
    ui.applyData({1, "fixture", {{"logs", logs}}, ui.dataDemands().generations});
    if (ui.liveLogs()->count() != 100)
        return 53;
    if (!click(host, QStringLiteral("pause_logs")) || ui.liveLogs()->count() != 101)
        return 54;
    QTest::qWait(30);
    if (!view->property("atYEnd").toBool())
        return 55;
    if (!click(host, QStringLiteral("preview_log_export")))
        return 84;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"status", "Ready"},
                     {"features", QJsonObject{{"logExportPreview",
                                               QJsonObject{{"token", "preview-token"},
                                                           {"messagesHidden", true},
                                                           {"text", "frozen redacted logs"}}}}}});
    QTest::qWait(30);
    auto* logDialog = host.window()->findChild<QObject*>("log_export_dialog");
    auto* logPreview = host.window()->findChild<QObject*>("log_export_preview");
    auto* sourceFilter = host.window()->findChild<QObject*>("log_source_filter");
    if (!logDialog || !logDialog->property("visible").toBool() || !logPreview ||
        logPreview->property("text") != "frozen redacted logs" || !sourceFilter ||
        sourceFilter->property("count").toInt() != 4)
        return 85;
    auto* hideMessages = host.window()->findChild<QObject*>("log_export_hide_messages");
    auto* logPage = host.window()->findChild<QObject*>("logs_page");
    if (!hideMessages || !logPage || !logPage->property("exportReady").toBool() ||
        !hideMessages->setProperty("checked", false) ||
        !QMetaObject::invokeMethod(hideMessages, "toggled") ||
        logPage->property("exportReady").toBool())
        return 87;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"status", "Ready"},
                     {"features", QJsonObject{{"logExportPreview",
                                               QJsonObject{{"token", "new-preview-token"},
                                                           {"messagesHidden", false},
                                                           {"text", "same frozen range"}}}}}});
    if (!logPage->property("exportReady").toBool())
        return 88;
    QMetaObject::invokeMethod(logDialog, "reject");
    // Moving away from the tail changes following only; reception remains active.
    if (!QMetaObject::invokeMethod(view, "movementStarted") ||
        !ui.dataDemands().active.contains("log_feed"))
        return 86;
    if (!click(host, QStringLiteral("pause_logs")))
        return 66;
    const application::RuntimeUpdate oldLogs{
        1, "fixture", {{"logs", logs}}, ui.dataDemands().generations};
    host.unloadWindow();
    ui.applyData(oldLogs);
    if (ui.liveLogs()->count() != 0 || ui.activeDemandCount() != 0 ||
        ui.diagnosticSnapshot()
                .value("dataLifecycle")
                .toObject()
                .value("pendingLogCount")
                .toInt() != 0)
        return 67;
    // Filter state intentionally survives page destruction; isolate the epoch assertion.
    ui.liveConnections()->setQuery({});
    if (!host.showWindow())
        return 57;
    QTest::qWait(50);
    if (!ui.navigate("connections"))
        return 68;
    QTest::qWait(60);
    ui.applyData(oldConnections);
    if (ui.liveConnections()->count() != 0)
        return 69;
    ui.applyData({2,
                  "next",
                  {{"connections", QJsonArray{QJsonObject{{"id", "next"}, {"epoch", "next"}}}}},
                  ui.dataDemands().generations});
    ui.applyData(oldConnections);
    if (ui.liveConnections()->captureIds() != QStringList{"next"})
        return 56;
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"selectedId", "proxy_fixture"},
                     {"outbound", QJsonObject{{"policy", "manual"}}}});
    ui.saveState("proxies:proxy_fixture", {{"groups", QStringList{}}});
    if (!ui.navigate("proxies"))
        return 72;
    QTest::qWait(60);
    auto* loader = host.window()->findChild<QObject*>("active_page_host");
    auto* proxies = loader ? loader->property("item").value<QObject*>() : nullptr;
    if (!proxies ||
        !proxies->property("expandedGroups").value<QJSValue>().toVariant().toList().isEmpty())
        return 73;
    QJsonArray members, nodes;
    for (int i = 0; i < 500; ++i) {
        const auto name = QStringLiteral("node_%1").arg(i);
        members.append(name);
        nodes.append(QJsonObject{{"name", name}, {"type", "HTTP"}});
    }
    ui.applyData({2,
                  "next",
                  {{"groups", QJsonArray{QJsonObject{{"name", "manual"},
                                                     {"type", "Selector"},
                                                     {"canSelect", true},
                                                     {"members", members}}}},
                   {"nodes", nodes},
                   {"delays", QJsonObject{{"node_0", "Timeout"}}}},
                  ui.dataDemands().generations});
    proxies->setProperty("expandedGroups", QStringList{"manual"});
    QTest::qWait(60);
    // Count visual children too: Repeater ownership differs from visual parenting.
    const auto controls = [&](auto&& self, QQuickItem* item) -> int {
        int count = item->objectName() == QStringLiteral("test_node") ? 1 : 0;
        for (auto* child : item->childItems())
            count += self(self, child);
        return count;
    };
    const auto instantiated = controls(controls, host.window()->contentItem());
    if (instantiated <= 0 || instantiated > 80)
        return 74;
    const auto findGrid = [&](auto&& self, QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == QStringLiteral("proxy_node_grid"))
            return item;
        for (auto* child : item->childItems())
            if (auto* found = self(self, child))
                return found;
        return nullptr;
    };
    auto* grid = findGrid(findGrid, host.window()->contentItem());
    if (!grid || !QMetaObject::invokeMethod(grid, "focusMember", Q_ARG(QVariant, 499)))
        return 75;
    QTest::qWait(60);
    if (grid->property("endIndex").toInt() != 500 ||
        controls(controls, host.window()->contentItem()) > 80)
        return 76;
    proxies->setProperty("expandedGroups", QStringList{});
    QTest::qWait(30);
    if (controls(controls, host.window()->contentItem()) != 0)
        return 78;
    if (!ui.navigate("settings"))
        return 81;
    const QJsonObject savedSettings{
        {"mode", "rule"}, {"mixedPort", 7890}, {"controllerPort", 9090}, {"dnsMode", "profile"}};
    auto requestedSettings = savedSettings;
    requestedSettings.insert("mixedPort", 7891);
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"ready", true},
                     {"selectionGeneration", "7"},
                     {"preferences", requestedSettings},
                     {"settingsState", QJsonObject{{"saved", savedSettings},
                                                   {"requested", requestedSettings},
                                                   {"effective", savedSettings},
                                                   {"running", true},
                                                   {"pending", true}}}});
    QTest::qWait(50);
    auto* settingsPage = host.window()->findChild<QObject*>("settings_page");
    auto* settingsText = host.window()->findChild<QObject*>("network_settings_state");
    if (!settingsPage || !settingsText ||
        !settingsText->property("text").toString().contains("7891") ||
        !settingsText->property("text").toString().contains("7890") ||
        !QMetaObject::invokeMethod(settingsPage, "requestNetworkSettings"))
        return 82;
    auto* settingsDialog = host.window()->findChild<QObject*>("network_settings_confirmation");
    auto pending = settingsPage->property("pendingNetworkArgs");
    if (pending.metaType() == QMetaType::fromType<QJSValue>())
        pending = pending.value<QJSValue>().toVariant();
    const auto capturedSettings = pending.toList();
    if (!settingsDialog || !settingsDialog->property("visible").toBool() ||
        capturedSettings.size() != 6 || capturedSettings[1] != "7891" ||
        capturedSettings[4] != "next" || capturedSettings[5] != "7")
        return 83;
    QMetaObject::invokeMethod(settingsDialog, "reject");
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"features", QJsonObject{{"listenerPortCandidate",
                                               QJsonObject{{"token", "port_preview"},
                                                           {"mixedPort", 7893},
                                                           {"controllerPort", 9092}}}}}});
    QTest::qWait(40);
    pending = settingsPage->property("pendingNetworkArgs");
    if (pending.metaType() == QMetaType::fromType<QJSValue>())
        pending = pending.value<QJSValue>().toVariant();
    if (!settingsDialog->property("visible").toBool() || pending.toList().value(1) != "7893" ||
        pending.toList().value(2) != "9092" || pending.toList().value(4) != "next")
        return 181;
    QMetaObject::invokeMethod(settingsDialog, "reject");
    const QJsonObject maintenancePreview{
        {"token", "fixed-support"}, {"kind", "diagnostics"}, {"text", "{\"schemaVersion\":1}"}};
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"features", QJsonObject{{"maintenancePreview", maintenancePreview}}}});
    QTest::qWait(40);
    auto* maintenanceDialog = host.window()->findChild<QObject*>("maintenance_confirmation");
    if (!maintenanceDialog || !maintenanceDialog->property("visible").toBool())
        return 139;
    host.window()->resize(360, 480);
    QTest::qWait(40);
    if (maintenanceDialog->property("width").toReal() > 328 ||
        maintenanceDialog->property("height").toReal() > 448)
        return 140;
    ui.setUiVisible(false);
    if (maintenanceDialog->property("visible").toBool())
        return 141;
    ui.setUiVisible(true);
    ui.applyControl(
        {{"publicationGeneration", "2"}, {"epoch", "next"}, {"features", QJsonObject{}}});
    const QJsonObject backupPreview{
        {"token", "fixed-preview"},
        {"scope", "source"},
        {"decodedBytes", "1234"},
        {"profiles", QJsonArray{QJsonObject{
                         {"name", QString(80, 'x')}, {"snapshot", true}, {"overrides", true}}}}};
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"features", QJsonObject{{"backupPreview", backupPreview}}}});
    QTest::qWait(40);
    auto* backupDialog = host.window()->findChild<QObject*>("backup_restore_confirmation");
    if (!backupDialog || !backupDialog->property("visible").toBool())
        return 84;
    host.window()->resize(360, 480);
    QTest::qWait(40);
    if (backupDialog->property("width").toReal() > 328 ||
        backupDialog->property("height").toReal() > 448)
        return 85;
    ui.setUiVisible(false);
    if (backupDialog->property("visible").toBool())
        return 86;
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"features", QJsonObject{{"backupPreview", backupPreview}}}});
    QTest::qWait(20);
    if (backupDialog->property("visible").toBool())
        return 87;
    ui.setUiVisible(true);
    QTest::qWait(150);
    if (host.qmlErrorCount() != 0 || !host.contentReady())
        return 58;
    std::puts("Runtime lists: leases, tab/hide/destruction release, late delivery, captured close, "
              "log pause/follow, empty expansion and 500-node viewport passed");
    return 0;
}
int runPageMemoryProbe(ApplicationHost& host) {
#ifndef Q_OS_WIN
    Q_UNUSED(host)
    return 77;
#else
    // Isolate Qt/QML from subscription IO, SettingsService, core and platform actions.
    // Keep the same fonts, icons, window and fixture for hardware/software comparisons.
    struct Counters : PROCESS_MEMORY_COUNTERS_EX {
        SIZE_T privateWorkingSetSize = 0;
        ULONGLONG sharedCommitUsage = 0;
    };
    struct Sample {
        quint64 privateWorkingSet = 0, workingSet = 0, privateCommit = 0, cpu100ns = 0;
        QJsonObject json() const {
            return {{"privateWorkingSet", QString::number(privateWorkingSet)},
                    {"workingSet", QString::number(workingSet)},
                    {"privateCommit", QString::number(privateCommit)}};
        }
    };
    const auto read = [](Sample& sample) {
        Counters counters;
        counters.cb = sizeof(counters);
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessMemoryInfo(GetCurrentProcess(),
                                  reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                  sizeof(counters)) ||
            !GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
            return false;
        const auto ticks = [](FILETIME time) {
            return (static_cast<quint64>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        };
        sample = {counters.privateWorkingSetSize, counters.WorkingSetSize, counters.PrivateUsage,
                  ticks(kernel) + ticks(user)};
        return true;
    };
    const auto page = [&host] {
        auto* loader = host.window()->findChild<QObject*>(QStringLiteral("active_page_host"));
        return loader ? loader->property("item").value<QObject*>() : nullptr;
    };
    // Repeater delegates may have a visual parent different from their QObject parent.
    const auto objects = [](QObject* root) {
        QSet<QObject*> found;
        QList<QObject*> pending{root};
        while (!pending.isEmpty()) {
            auto* object = pending.takeLast();
            if (!object || found.contains(object))
                continue;
            found.insert(object);
            pending.append(object->children());
            if (auto* item = qobject_cast<QQuickItem*>(object))
                for (auto* child : item->childItems())
                    pending.append(child);
        }
        return found;
    };
    const auto nodeControls = [&] {
        int count = 0;
        for (auto* object : objects(host.window()))
            if (object->objectName() == QStringLiteral("test_node"))
                ++count;
        return count;
    };
    QElapsedTimer elapsed;
    elapsed.start();
    const auto stage = [&](const char* name, int milliseconds, bool scroll = false) {
        Sample before, endpoint, peak;
        if (!read(before))
            return false;
        QElapsedTimer clock;
        clock.start();
        int samples = 0;
        while (clock.elapsed() < milliseconds) {
            if (scroll && page()) {
                for (auto* object : objects(page())) {
                    auto* item = qobject_cast<QQuickItem*>(object);
                    if (item && item->property("contentY").isValid()) {
                        const auto extent = std::max(
                            0.0, item->property("contentHeight").toDouble() - item->height());
                        item->setProperty("contentY", extent * ((clock.elapsed() / 400) % 5) / 4);
                        break;
                    }
                }
            }
            QTest::qWait(20);
            if (!read(endpoint))
                return false;
            peak.privateWorkingSet = std::max(peak.privateWorkingSet, endpoint.privateWorkingSet);
            peak.workingSet = std::max(peak.workingSet, endpoint.workingSet);
            peak.privateCommit = std::max(peak.privateCommit, endpoint.privateCommit);
            ++samples;
        }
        const QJsonObject result{
            {"stage", QString::fromLatin1(name)},
            {"elapsedMs", elapsed.elapsed()},
            {"stageMs", clock.elapsed()},
            {"cpuMs", static_cast<double>(endpoint.cpu100ns - before.cpu100ns) / 10000},
            {"samples", samples},
            {"endpoint", endpoint.json()},
            {"sampledPeak", peak.json()},
            {"windowObjects", objects(host.window()).size()},
            {"pageObjects", objects(page()).size()},
            {"nodeControls", nodeControls()},
            {"modelGroups", host.state().groups().size()},
            {"modelNodes", host.state().nodes().size()}};
        std::printf("Page memory: %s\n",
                    QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
        return host.contentReady() && host.qmlErrorCount() == 0;
    };
    auto& ui = host.state();
    QTest::qWait(600);
    const QJsonObject environment{
        {"qt", QString::fromLatin1(qVersion())},
        {"graphicsApi", static_cast<int>(host.window()->rendererInterface()->graphicsApi())},
        {"font", QFontInfo(QGuiApplication::font()).family()},
        {"width", host.window()->width()},
        {"height", host.window()->height()},
        {"dpr", host.window()->devicePixelRatio()},
        {"nodes", 500},
        {"groups", 3}};
    std::printf("Page environment: %s\n",
                QJsonDocument(environment).toJson(QJsonDocument::Compact).constData());
    if (!stage("startup_home", 1600))
        return 100;
    QJsonArray members, nodes, groups;
    for (int i = 0; i < 500; ++i) {
        const auto name = QStringLiteral("日本高速%1|CTCU|0.5x").arg(i, 2, 10, QLatin1Char('0'));
        members.append(name);
        nodes.append(QJsonObject{{"name", name}, {"type", "HTTP"}, {"udp", true}});
    }
    QStringList groupNames;
    for (int i = 0; i < 3; ++i) {
        const auto name = QStringLiteral("代理组%1").arg(i);
        groupNames.append(name);
        groups.append(QJsonObject{{"name", name},
                                  {"type", "Selector"},
                                  {"members", members},
                                  {"current", members.first()},
                                  {"canSelect", true}});
    }
    ui.applyControl(
        {{"publicationGeneration", "1"},
         {"epoch", "fixture"},
         {"ready", true},
         {"status", "Ready"},
         {"selectedId", "page_memory"},
         {"outbound", QJsonObject{{"policy", groupNames.first()}, {"node", members.first()}}}});
    ui.applyData(
        {1,
         "fixture",
         {{"groups", groups},
          {"nodes", nodes},
          {"profiles", QJsonArray{QJsonObject{{"id", "page_memory"}, {"name", "公开切页夹具"}}}}},
         ui.dataDemands().generations});
    const QPointer<QObject> firstHome = page();
    if (!ui.navigate("proxies") || !stage("proxies_one_group", 2200, true))
        return 101;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!firstHome.isNull() || nodeControls() <= 0 || nodeControls() > 80)
        return 102;
    const QPointer<QObject> firstProxies = page();
    if (!page() || !page()->setProperty("expandedGroups", groupNames) ||
        !stage("proxies_three_groups", 2200, true) || nodeControls() <= 0 || nodeControls() > 80)
        return 103;
    if (!ui.navigate("subscriptions") || !stage("subscriptions", 2200))
        return 104;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!firstProxies.isNull() || nodeControls() != 0)
        return 105;
    auto* editor = host.window()->findChild<QObject*>(QStringLiteral("profile_editor"));
    if (!editor || !QMetaObject::invokeMethod(editor, "edit", Q_ARG(QVariant, "page_memory"),
                                              Q_ARG(QVariant, "source")))
        return 117;
    ui.applyData({1,
                  "fixture",
                  {{"editor", QJsonObject{{"id", "page_memory"},
                                          {"kind", "source"},
                                          {"token", "memory_editor"},
                                          {"text", QString(512 * 1024, QLatin1Char('#'))},
                                          {"name", "Public editor fixture"}}}},
                  ui.dataDemands().generations});
    if (!stage("editor_512k", 2200) || ui.editor().isEmpty())
        return 118;
    QMetaObject::invokeMethod(editor, "close");
    if (!stage("editor_released", 500) || !ui.editor().isEmpty())
        return 119;
    const QPointer<QObject> subscriptions = page();
    if (!ui.navigate("home") || !stage("home_after_pages", 3000))
        return 106;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!subscriptions.isNull())
        return 107;
    if (!ui.navigate("connections"))
        return 109;
    QJsonArray connectionRows;
    for (int i = 0; i < 1000; ++i)
        connectionRows.append(entry(QString::number(i), QStringLiteral("host%1.example").arg(i)));
    ui.applyData({1, "fixture", {{"connections", connectionRows}}, ui.dataDemands().generations});
    if (!stage("connections_1000", 2200, true) || ui.liveConnections()->count() != 1000)
        return 110;
    const QPointer<QObject> connections = page();
    if (!ui.navigate("home"))
        return 111;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!connections.isNull() || ui.liveConnections()->count() != 0)
        return 112;
    if (!ui.navigate("logs"))
        return 113;
    QJsonArray logRows;
    for (int i = 0; i < 2000; ++i)
        logRows.append(
            QJsonObject{{"localSeq", QString::number(i)},
                        {"time", "2026-10-28T00:00:00Z"},
                        {"source", "Core"},
                        {"level", "info"},
                        {"message", QStringLiteral("公开日志夹具%1 example.test").arg(i)}});
    ui.applyData({1, "fixture", {{"logs", logRows}}, ui.dataDemands().generations});
    if (!stage("logs_2000", 2200, true) || ui.liveLogs()->count() != 2000)
        return 114;
    const QPointer<QObject> logs = page();
    if (!ui.navigate("home"))
        return 115;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!logs.isNull() || ui.liveLogs()->count() != 0)
        return 116;
    if (!ui.navigate("proxies") || !stage("proxies_reentry", 1800, true) || !ui.navigate("home") ||
        !stage("home_after_reentry", 2200))
        return 108;
    std::puts("Page memory: old pages destroyed, node controls released, no core or settings IO");
    return 0;
#endif
}
} // namespace quickclash
