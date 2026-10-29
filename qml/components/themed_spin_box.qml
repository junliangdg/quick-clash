import QtQuick
import QtQuick.Controls
import QuickClash.Ui

SpinBox {
    id: control
    required property ThemeTokens theme
    property bool invalid: false
    font.family: theme.fontFamily
    font.pixelSize: theme.bodySize
    implicitHeight: theme.controlHeight
    implicitWidth: theme.scaledSize(144)
    leftPadding: theme.scaledSize(36)
    rightPadding: theme.scaledSize(36)
    activeFocusOnTab: true
    contentItem: TextInput {
        text: control.displayText
        font: control.font
        color: control.enabled ? control.theme.text : control.theme.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhDigitsOnly
        selectionColor: control.theme.accent
        selectedTextColor: control.theme.accentForeground
        selectByMouse: true
    }
    up.indicator: StateSurface {
        theme: control.theme
        x: control.width - width
        implicitWidth: control.theme.scaledSize(32)
        implicitHeight: control.theme.controlHeight
        hovered: control.up.hovered
        down: control.up.pressed
        Text {
            anchors.centerIn: parent
            text: "+"
            color: control.theme.text
            font.pixelSize: control.theme.scaledSize(18)
        }
    }
    down.indicator: StateSurface {
        theme: control.theme
        implicitWidth: control.theme.scaledSize(32)
        implicitHeight: control.theme.controlHeight
        hovered: control.down.hovered
        down: control.down.pressed
        Text {
            anchors.centerIn: parent
            text: "−"
            color: control.theme.text
            font.pixelSize: control.theme.scaledSize(18)
        }
    }
    background: StateSurface {
        theme: control.theme
        color: control.enabled ? theme.surface : theme.inset
        border.width: 1
        border.color: control.invalid ? theme.error : theme.controlOutline
        focused: control.activeFocus
    }
}
