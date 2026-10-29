import QtQuick
import QtQuick.Controls
import QuickClash.Ui

ToolTip {
    id: control
    required property ThemeTokens theme
    delay: 500
    padding: 8
    contentItem: Text {
        text: control.text
        color: control.theme.text
        font.pixelSize: control.theme.scaledSize(12)
        font.family: control.theme.fontFamily
        wrapMode: Text.WordWrap
    }
    background: Rectangle {
        color: control.theme.surface
        border.color: control.theme.controlOutline
        border.width: 1
        radius: control.theme.controlRadius
    }
}
