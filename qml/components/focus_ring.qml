import QtQuick
import QuickClash.Ui

Rectangle {
    required property ThemeTokens theme
    property bool inset: false
    property real clipLeft: 0
    property real clipRight: 0
    property real surfaceRadius: theme.controlRadius
    objectName: "focus_ring"
    anchors.fill: parent
    anchors.margins: inset ? theme.focusGap : -theme.focusExtent
    anchors.leftMargin: (inset ? theme.focusGap : -theme.focusExtent) + clipLeft
    anchors.rightMargin: (inset ? theme.focusGap : -theme.focusExtent) + clipRight
    radius: Math.max(0, surfaceRadius + (inset ? -theme.focusGap : theme.focusExtent))
    color: "transparent"
    border.width: theme.focusWidth
    border.color: theme.accent
    z: 10
}
