pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QuickClash.Ui

ScrollView {
    id: control
    required property ThemeTokens theme
    // One body owns layout/implicitHeight; the scroll container owns its safe geometry.
    required property Item form
    readonly property int focusMargin: theme.focusMargin
    readonly property real formWidth: Math.max(0, availableWidth - 2 * focusMargin)
    // PageHost extends only page-root views into the existing shell margin.
    readonly property real pageLeftOutset: focusMargin + leftPadding
    readonly property real pageRightOutset: focusMargin + rightPadding
    contentWidth: availableWidth
    contentHeight: form ? form.implicitHeight + 2 * focusMargin : 0
    // Stable width avoids a feedback loop between wrapping and scrollbar visibility.
    leftPadding: mirrored ? theme.scrollBarWidth : 0
    rightPadding: mirrored ? 0 : theme.scrollBarWidth
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical: ScrollBar {
        id: verticalBar
        objectName: "themed_vertical_scroll_bar"
        // Replacing the style's bar also replaces its viewport geometry bindings.
        parent: control
        width: control.theme.scrollBarWidth
        x: control.mirrored ? 0 : control.width - width
        y: control.topPadding
        height: control.availableHeight
        active: control.ScrollBar.horizontal.active
    }
    Binding {
        target: control.form
        property: "x"
        value: control.focusMargin
    }
    Binding {
        target: control.form
        property: "y"
        value: control.focusMargin
    }
    Binding {
        target: control.form
        property: "width"
        value: control.formWidth
    }
}
