import QtQuick
import QtQuick.Controls
import QuickClash.Ui

Switch {
    id: control
    required property ThemeTokens theme
    font.family: theme.fontFamily
    font.pixelSize: theme.bodySize
    implicitHeight: Math.max(theme.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    spacing: 10
    padding: 4
    activeFocusOnTab: true
    indicator: StateSurface {
        theme: control.theme
        implicitWidth: 36
        implicitHeight: 20
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 10
        focused: control.activeFocus
        color: !control.enabled ? theme.inset : control.checked ? (control.down ? theme.primaryPressed : control.hovered ? theme.primaryHover : theme.accent) : control.down ? theme.pressed : theme.inset
        border.width: control.checked && control.enabled ? 0 : 1
        border.color: theme.controlOutline
        Rectangle {
            x: control.checked ? 19 : 3
            y: 3
            width: 14
            height: 14
            radius: 7
            color: !control.enabled ? control.theme.muted : control.checked ? control.theme.accentForeground : control.theme.muted
        }
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? control.theme.text : control.theme.muted
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
