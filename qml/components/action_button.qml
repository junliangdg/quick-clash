import QtQuick
import QtQuick.Controls
import QuickClash.Ui

Button {
    id: control
    font.family: control.theme.fontFamily
    required property ThemeTokens theme
    property bool prominent: false
    implicitHeight: 36
    leftPadding: 14
    rightPadding: 14
    activeFocusOnTab: true
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? (control.prominent ? "#ffffff" : control.theme.text) : control.theme.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 10
        color: control.prominent ? (control.theme.dark ? "#286bc7" : "#1767d8") : (control.hovered ? control.theme.accentSurface : control.theme.inset)
        opacity: control.enabled ? 1 : 0.55
        border.width: control.activeFocus ? 2 : 0
        border.color: control.theme.accent
    }
}
