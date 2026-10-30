#include "application_host.h"
#include "host_probe.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickItem>
#include <QTest>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace quickclash {
namespace {
bool tabTo(QQuickWindow* window, const QString& name) {
    for (int attempt = 0; attempt < 80; ++attempt) {
        if (window->activeFocusItem() && window->activeFocusItem()->objectName() == name) {
            return true;
        }
        QTest::keyClick(window, Qt::Key_Tab);
        QTest::qWait(1);
    }
    return false;
}
} // namespace
int runKeyboardProbe(ApplicationHost& host) {
    host.state().setReplay(true);
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "Keyboard probe: %s\n", message);
        }
        return condition;
    };
    QTest::qWait(100);
    if (!require(tabTo(host.window(), QStringLiteral("navigation_proxies")), "tab to proxy page")) {
        return 2;
    }
    QTest::keyClick(host.window(), Qt::Key_Space);
    QTest::qWait(100);
    if (!require(host.state().currentPage() == QStringLiteral("proxies") && host.contentReady(),
                 "keyboard activates navigation")) {
        return 3;
    }
    if (!require(tabTo(host.window(), QStringLiteral("list_filter")), "tab to filter")) {
        return 4;
    }
    for (const auto character : QStringLiteral("node_2")) {
        QTest::keyClick(host.window(), character.toLatin1());
    }
    QTest::qWait(200);
    if (!require(host.state()
                         .readState(QStringLiteral("proxies"))
                         .value(QStringLiteral("filter"))
                         .toString() == QStringLiteral("node_2"),
                 "keyboard edits filter")) {
        return 5;
    }
    QTest::keyClick(host.window(), Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(host.window(), Qt::Key_Backspace);
    QTest::qWait(200);
    if (!require(host.state()
                     .readState(QStringLiteral("proxies"))
                     .value(QStringLiteral("filter"))
                     .toString()
                     .isEmpty(),
                 "keyboard clears filter")) {
        return 6;
    }
    QTest::keyClick(host.window(), Qt::Key_W, Qt::ControlModifier);
    QTest::qWait(50);
    auto* dialog = host.window()->findChild<QObject*>(QStringLiteral("close_confirmation"));
    if (!require(dialog && dialog->property("visible").toBool() && host.window()->isVisible(),
                 "close shortcut opens safe no-tray confirmation")) {
        return 7;
    }
    QTest::keyClick(host.window(), Qt::Key_Escape);
    QTest::qWait(50);
    if (!require(!dialog->property("visible").toBool() && host.window()->isVisible(),
                 "escape dismisses confirmation")) {
        return 8;
    }
    std::puts("Keyboard navigation, filter and close confirmation passed");
    return 0;
}
int runResponsiveProbe(ApplicationHost& host) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    auto* window = host.window();
    const auto generation = host.engineGeneration();
    auto* side = window->findChild<QQuickItem*>(QStringLiteral("side_navigation"));
    auto* bottom = window->findChild<QQuickItem*>(QStringLiteral("bottom_navigation"));
    if (!side || !bottom)
        return 20;
    host.state().saveState(QStringLiteral("proxies"), {{QStringLiteral("filter"), "retained"}});
    // Collapsing the desktop sidebar must not replace the active page or its demands.
    for (const int width : {1160, 800}) {
        window->resize(width, 780);
        QTest::qWait(30);
        auto* loader = window->findChild<QQuickItem*>(QStringLiteral("active_page_host"));
        auto* toggle = window->findChild<QQuickItem*>(QStringLiteral("navigation_toggle"));
        if (!loader || !toggle || !tabTo(window, QStringLiteral("navigation_toggle")))
            return 30;
        const QPointer<QObject> content = loader->property("item").value<QObject*>();
        const auto demandCount = host.state().activeDemandCount();
        const auto streamCount = host.state().activeStreamCount();
        for (const bool expanded : {width < 1000, width >= 1000}) {
            QTest::keyClick(window, Qt::Key_Space);
            QTest::qWait(30);
            if (window->property("wideNavigation").toBool() != expanded ||
                std::abs(side->width() - (expanded ? 192.0 : 64.0)) > 0.5 || !content ||
                loader->property("item").value<QObject*>() != content.data() ||
                host.state().activeDemandCount() != demandCount ||
                host.state().activeStreamCount() != streamCount ||
                host.engineGeneration() != generation)
                return 31;
        }
    }
    for (const auto width : {1160, 600, 599, 360, 1160}) {
        std::printf("Checking navigation width %d\n", width);
        const auto previousPage = host.state().currentPage();
        window->resize(width, 780);
        QTest::qWait(50);
        if (window->width() != width || side->isVisible() != (width >= 600) ||
            bottom->isVisible() != (width < 600) || host.state().currentPage() != previousPage)
            return 21;
        for (const auto& entry : host.state().navigation()) {
            if (!host.state().navigate(entry.toMap().value(QStringLiteral("pageId")).toString()))
                return 22;
            QTest::qWait(30);
            if (!host.contentReady())
                return 23;
        }
    }
    window->resize(360, 480);
    QTest::qWait(30);
    if (window->height() != 480 || !bottom->isVisible())
        return 26;
    window->resize(360, 780);
    QTest::qWait(50);
    if (!tabTo(window, QStringLiteral("bottom_navigation_proxies")))
        return 24;
    QTest::keyClick(window, Qt::Key_Space);
    QTest::qWait(30);
    if (host.state().currentPage() != QStringLiteral("proxies") ||
        host.engineGeneration() != generation || host.qmlErrorCount() != 0 ||
        host.state().readState(QStringLiteral("proxies")).value(QStringLiteral("filter")) !=
            "retained")
        return 25;
    // Cross both breakpoints continuously without changing the route. A layout
    // switch must preserve the loaded page and demand ownership.
    for (const auto& page : {QStringLiteral("home"), QStringLiteral("proxies")}) {
        std::printf("Continuous resize page %s\n", page.toUtf8().constData());
        if (!host.state().navigate(page))
            return 27;
        QTest::qWait(30);
        auto* loader = window->findChild<QQuickItem*>(QStringLiteral("active_page_host"));
        if (!loader)
            return 28;
        const QPointer<QObject> content = loader->property("item").value<QObject*>();
        const auto demandCount = host.state().activeDemandCount();
        const auto streamCount = host.state().activeStreamCount();
        std::array<qint64, 80> samples{};
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const int phase = static_cast<int>(index % 40);
            const int width = phase < 20 ? 1120 - phase * 40 : 360 + (phase - 20) * 40;
            QElapsedTimer elapsed;
            elapsed.start();
            window->resize(width, 780);
            QTest::qWait(1);
            samples[index] = elapsed.nsecsElapsed() / 1000;
            if (!content || loader->property("item").value<QObject*>() != content.data() ||
                host.state().currentPage() != page || host.engineGeneration() != generation ||
                host.state().activeDemandCount() != demandCount ||
                host.state().activeStreamCount() != streamCount || !host.contentReady()) {
                std::fprintf(stderr,
                             "Resize invariant failed: page=%s width=%d step=%zu sameItem=%d "
                             "generation=%d demands=%d/%d streams=%d/%d ready=%d errors=%d "
                             "host=%s elapsed=%lld us\n",
                             page.toUtf8().constData(), width, index,
                             content &&
                                 loader->property("item").value<QObject*>() == content.data(),
                             host.engineGeneration(), host.state().activeDemandCount(), demandCount,
                             host.state().activeStreamCount(), streamCount, host.contentReady(),
                             host.qmlErrorCount(), host.state().hostState().toUtf8().constData(),
                             samples[index]);
                return 29;
            }
        }
        std::sort(samples.begin(), samples.end());
        std::printf("Resize event processing (%s, 80 steps): median=%lld us p95=%lld us "
                    "max=%lld us; page/demands retained\n",
                    page.toUtf8().constData(), samples[40], samples[75], samples.back());
    }
    std::puts("Responsive 360/599/600/1160, all routes, bottom keyboard and retained state passed");
    return 0;
}
} // namespace quickclash
