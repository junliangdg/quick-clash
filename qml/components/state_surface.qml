import QtQuick
import QuickClash.Ui

Rectangle {
    id: root
    required property ThemeTokens theme
    property bool selected: false
    property bool hovered: false
    property bool down: false
    property bool focused: false
    radius: theme.controlRadius
    color: theme.stateColor(selected, hovered, down)
    FocusRing {
        theme: root.theme
        visible: root.focused
    }
}
