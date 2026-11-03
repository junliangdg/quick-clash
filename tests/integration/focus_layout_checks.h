#pragma once

class QQuickItem;
class QQuickWindow;

namespace quickclash::test {
// No runtime/platform commands. These assertions inspect real QML geometry.
bool checkScrollLayouts(QQuickWindow* window);
bool checkPageAlignment(QQuickWindow* window);
bool checkVisibleFocusFrames(QQuickWindow* window);
bool focusInScroll(QQuickItem* control);
} // namespace quickclash::test
