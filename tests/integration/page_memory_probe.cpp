#include "page_memory_probe.h"
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
#include <cmath>
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
#ifdef Q_OS_WIN
namespace {
QJsonObject entry(const QString& id, const QString& host) {
    return {
        {"id", id}, {"epoch", "fixture"}, {"metadata", QJsonObject{{"host", host}}}, {"start", id}};
}
} // namespace
#endif
int runPageMemoryProbe(ApplicationHost& host) {
#ifndef Q_OS_WIN
    Q_UNUSED(host)
    return 77;
#else
    // Isolate Qt/QML from subscription IO, SettingsService, core and platform actions.
    // Keep the same fonts, icons, window and fixture for hardware/software comparisons.
    // A helper launched with SW_HIDE must transition visibility again before Windows can
    // expose it; otherwise a hidden scene graph is not representative of the foreground.
    host.window()->hide();
    host.window()->show();
    if (!QTest::qWaitForWindowExposed(host.window(), 5000))
        return 119;
    int fixtureNodes = 500;
    const auto arguments = QCoreApplication::arguments();
    for (const auto& argument : arguments) {
        if (argument.startsWith(QStringLiteral("--page-memory-nodes="))) {
            bool valid = false;
            const auto requested = argument.mid(20).toInt(&valid);
            if (!valid || requested < 1 || requested > 5000)
                return 120;
            fixtureNodes = requested;
        }
    }
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
    // Diagnostic-only smooth scroll: a manual run can set QUICKCLASH_PROBE_SCROLL=smooth
    // to separate per-frame rendering cost from the row-jump cost of the default probe.
    const bool smoothScroll = qEnvironmentVariable("QUICKCLASH_PROBE_SCROLL") == "smooth";
    const bool traceScroll = qEnvironmentVariableIsSet("QUICKCLASH_PROBE_TRACE");
    const auto stage = [&](const char* name, int milliseconds, bool scroll = false) {
        Sample before, endpoint, peak;
        if (!read(before))
            return false;
        QElapsedTimer clock;
        clock.start();
        int samples = 0;
        QElapsedTimer frameClock;
        frameClock.start();
        QVector<qint64> frameTimes;
        const auto frameConnection =
            QObject::connect(host.window(), &QQuickWindow::frameSwapped, host.window(),
                             [&] { frameTimes.append(frameClock.nsecsElapsed()); });
        host.window()->requestUpdate();
        const int smoothMilliseconds = qEnvironmentVariableIntValue("QUICKCLASH_PROBE_SCROLL_MS");
        const int duration =
            scroll && smoothScroll
                ? std::max(milliseconds, smoothMilliseconds > 0 ? smoothMilliseconds : 6000)
                : milliseconds;
        qint64 nextTrace = 0;
        qint64 nextSample = 0;
        while (clock.elapsed() < duration) {
            if (scroll && page()) {
                auto* target =
                    host.window()->findChild<QQuickItem*>(QStringLiteral("proxy_group_list"));
                if (!target)
                    for (auto* object : objects(page())) {
                        auto* candidate = qobject_cast<QQuickItem*>(object);
                        if (candidate && candidate->property("contentY").isValid()) {
                            target = candidate;
                            break;
                        }
                    }
                if (target) {
                    const auto extent = std::max(0.0, target->property("contentHeight").toDouble() -
                                                          target->height());
                    if (smoothScroll) {
                        const double phase = std::fmod(clock.elapsed() / 6000.0, 2.0);
                        target->setProperty("contentY",
                                            extent * (phase < 1.0 ? phase : 2.0 - phase));
                    } else {
                        target->setProperty("contentY", extent * ((clock.elapsed() / 400) % 5) / 4);
                    }
                }
            }
            QTest::qWait(scroll ? 4 : 20);
            if (scroll && clock.elapsed() < nextSample)
                continue;
            nextSample = clock.elapsed() + 20;
            if (!read(endpoint))
                return false;
            if (traceScroll && scroll && clock.elapsed() >= nextTrace) {
                std::printf("Page memory trace: stage=%s elapsedMs=%lld pws=%llu commit=%llu\n",
                            name, static_cast<long long>(clock.elapsed()),
                            static_cast<unsigned long long>(endpoint.privateWorkingSet),
                            static_cast<unsigned long long>(endpoint.privateCommit));
                nextTrace = clock.elapsed() + 2000;
            }
            peak.privateWorkingSet = std::max(peak.privateWorkingSet, endpoint.privateWorkingSet);
            peak.workingSet = std::max(peak.workingSet, endpoint.workingSet);
            peak.privateCommit = std::max(peak.privateCommit, endpoint.privateCommit);
            ++samples;
        }
        QObject::disconnect(frameConnection);
        QJsonObject frameStats{{"swapped", static_cast<int>(frameTimes.size())}};
        if (frameTimes.size() > 1) {
            QVector<qint64> intervals;
            intervals.reserve(frameTimes.size() - 1);
            for (qsizetype i = 1; i < frameTimes.size(); ++i)
                intervals.append(frameTimes[i] - frameTimes[i - 1]);
            std::sort(intervals.begin(), intervals.end());
            const auto percentile = [&](double fraction) {
                return intervals[std::min(intervals.size() - 1,
                                          static_cast<qsizetype>(intervals.size() * fraction))] /
                       1.0e6;
            };
            const double median = percentile(0.5);
            int slowFrames = 0;
            for (const auto interval : intervals)
                slowFrames += interval > std::max<qint64>(1, static_cast<qint64>(median * 1.75e6));
            frameStats.insert("count", static_cast<int>(intervals.size()));
            frameStats.insert("medianMs", median);
            frameStats.insert("p95Ms", percentile(0.95));
            frameStats.insert("maxMs", static_cast<double>(intervals.last()) / 1.0e6);
            frameStats.insert("slowFrames", slowFrames);
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
            {"exposed", host.window()->isExposed()},
            {"visible", host.window()->isVisible()},
            {"frames", frameStats},
            {"modelGroups", host.state().groupCount()},
            {"modelNodes", host.state().nodeCount()}};
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
        {"nodes", fixtureNodes},
        {"groups", 3}};
    std::printf("Page environment: %s\n",
                QJsonDocument(environment).toJson(QJsonDocument::Compact).constData());
    if (!stage("startup_home", 1600))
        return 100;
    QJsonArray members, nodes, groups;
    for (int i = 0; i < fixtureNodes; ++i) {
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
    if (arguments.contains(QStringLiteral("--page-memory-home-only"))) {
        if (!stage("home_with_nodes", 2500))
            return 121;
        auto* card = host.window()->findChild<QQuickItem*>(QStringLiteral("home_proxy_card"));
        auto* viewport = card ? card->parentItem() : nullptr;
        while (viewport && !viewport->inherits("QQuickFlickable"))
            viewport = viewport->parentItem();
        auto* selector = host.window()->findChild<QObject*>(QStringLiteral("home_proxy_selector"));
        auto* popup = selector ? selector->property("popup").value<QObject*>() : nullptr;
        if (!card || !viewport || !popup)
            return 122;
        viewport->setProperty("contentY", card->y());
        QTest::qWait(100);
        if (!QMetaObject::invokeMethod(popup, "open") || !stage("home_popup_open", 1800))
            return 123;
        if (!QMetaObject::invokeMethod(popup, "close") || !stage("home_popup_closed", 1800))
            return 124;
        return 0;
    }
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
    if (host.window()->findChild<QObject*>(QStringLiteral("profile_editor")) ||
        !QMetaObject::invokeMethod(page(), "editProfile", Q_ARG(QVariant, "page_memory"),
                                   Q_ARG(QVariant, "source")))
        return 117;
    QPointer<QObject> editor = host.window()->findChild<QObject*>(QStringLiteral("profile_editor"));
    if (!editor)
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
    if (!stage("editor_released", 500) || !ui.editor().isEmpty() || !editor.isNull())
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
