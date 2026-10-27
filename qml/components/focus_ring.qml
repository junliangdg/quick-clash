import QtQuick
import QuickClash.Ui

Rectangle {
    required property ThemeTokens theme
    anchors.fill: parent
    anchors.margins: -(theme.focusGap + theme.focusWidth)
    radius: theme.controlRadius + theme.focusGap + theme.focusWidth
    color: "transparent"
    border.width: theme.focusWidth
    border.color: theme.accent
    z: 10
}
