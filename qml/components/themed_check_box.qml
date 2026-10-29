import QtQuick
import QtQuick.Controls
import QuickClash.Ui

CheckBox {
    id: control
    required property ThemeTokens theme
    Accessible.name: text
    font.family: theme.fontFamily
    font.pixelSize: theme.bodySize
    implicitHeight: Math.max(theme.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    spacing: 10
    padding: 4
    activeFocusOnTab: true
    indicator: StateSurface {
        theme: control.theme
        implicitWidth: 18
        implicitHeight: 18
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 4
        focused: control.activeFocus
        color: !control.enabled ? theme.inset : control.checked ? (control.down ? theme.primaryPressed : control.hovered ? theme.primaryHover : theme.accent) : control.hovered ? theme.hover : theme.surface
        border.width: control.checked ? 0 : 1
        border.color: theme.controlOutline
        Text {
            anchors.centerIn: parent
            text: control.checkState === Qt.PartiallyChecked ? "−" : "✓"
            visible: control.checkState !== Qt.Unchecked
            color: control.enabled ? control.theme.accentForeground : control.theme.muted
            font.pixelSize: control.theme.scaledSize(14)
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
