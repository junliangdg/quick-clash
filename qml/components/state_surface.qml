import QtQuick
import QuickClash.Ui

Rectangle {
    id: root
    required property ThemeTokens theme
    property bool selected: false
    property bool hovered: false
    property bool down: false
    property bool focused: false
    property bool insetFocus: false
    property color focusColor: theme.accent
    property real focusClipLeft: 0
    property real focusClipRight: 0
    radius: theme.controlRadius
    color: theme.stateColor(selected, hovered, down)
    FocusRing {
        theme: root.theme
        inset: root.insetFocus
        clipLeft: root.focusClipLeft
        clipRight: root.focusClipRight
        surfaceRadius: root.radius
        border.color: root.focusColor
        visible: root.focused
    }
}
