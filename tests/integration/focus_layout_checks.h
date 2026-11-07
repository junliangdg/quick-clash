#pragma once

class QQuickItem;
class QQuickWindow;

namespace quickclash::test {
// No runtime/platform commands. These assertions inspect real QML geometry.
bool checkScrollLayouts(QQuickWindow* window);
bool checkPageAlignment(QQuickWindow* window);
bool checkVisibleFocusFrames(QQuickWindow* window);
// Reveal and focus for interaction tests without imposing pixel geometry checks.
bool revealInScroll(QQuickItem* control);
bool focusInScroll(QQuickItem* control);
} // namespace quickclash::test
