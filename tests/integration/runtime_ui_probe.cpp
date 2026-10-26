#include "runtime_ui_probe.h"
#include "application_host.h"
#include <QElapsedTimer>
#include <QEvent>
#include <QFontInfo>
#include <QGuiApplication>
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
    section->setProperty("currentIndex", 1);
    QTest::qWait(30);
    if (ui.liveRules()->count() != 0 || ui.dataDemands().active.contains("rules") ||
        !ui.dataDemands().active.contains("providers"))
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
    if (host.qmlErrorCount() != 0 || !host.contentReady())
        return 58;
    std::puts("Runtime lists: leases, tab/hide/destruction release, late delivery, captured close, "
              "log pause/follow and reentry passed");
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
        {"nodes", 58},
        {"groups", 3}};
    std::printf("Page environment: %s\n",
                QJsonDocument(environment).toJson(QJsonDocument::Compact).constData());
    if (!stage("startup_home", 1600))
        return 100;
    QJsonArray members, nodes, groups;
    for (int i = 0; i < 58; ++i) {
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
    if (!firstHome.isNull() || nodeControls() != 58)
        return 102;
    const QPointer<QObject> firstProxies = page();
    if (!page() || !page()->setProperty("expandedGroups", groupNames) ||
        !stage("proxies_three_groups", 2200, true) || nodeControls() != 174)
        return 103;
    if (!ui.navigate("subscriptions") || !stage("subscriptions", 2200))
        return 104;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!firstProxies.isNull() || nodeControls() != 0)
        return 105;
    const QPointer<QObject> subscriptions = page();
    if (!ui.navigate("home") || !stage("home_after_pages", 3000))
        return 106;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!subscriptions.isNull())
        return 107;
    if (!ui.navigate("proxies") || !stage("proxies_reentry", 1800, true) || !ui.navigate("home") ||
        !stage("home_after_reentry", 2200))
        return 108;
    std::puts("Page memory: old pages destroyed, node controls released, no core or settings IO");
    return 0;
#endif
}
} // namespace quickclash
