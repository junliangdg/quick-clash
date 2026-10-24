#include "application_host.h"
#include "host_probe.h"
#include <QQuickItem>
#include <QTest>
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
} // namespace quickclash
