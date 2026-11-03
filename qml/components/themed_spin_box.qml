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
    // Keep the full hit target, but inset visual feedback so it cannot paint over the outline.
    up.indicator: Item {
        id: incrementTarget
        x: control.mirrored ? 0 : control.width - width
        implicitWidth: control.theme.scaledSize(32)
        implicitHeight: control.theme.controlHeight
        height: control.height
        StateSurface {
            objectName: "spin_box_increment_surface"
            anchors.fill: parent
            anchors.margins: control.theme.scaledSize(4)
            theme: control.theme
            radius: Math.max(0, control.theme.controlRadius - control.theme.scaledSize(2))
            hovered: incrementTarget.enabled && control.up.hovered
            down: incrementTarget.enabled && control.up.pressed
            Text {
                anchors.centerIn: parent
                text: "+"
                color: incrementTarget.enabled ? control.theme.text : control.theme.muted
                font.pixelSize: control.theme.scaledSize(18)
            }
        }
    }
    down.indicator: Item {
        id: decrementTarget
        x: control.mirrored ? control.width - width : 0
        implicitWidth: control.theme.scaledSize(32)
        implicitHeight: control.theme.controlHeight
        height: control.height
        StateSurface {
            objectName: "spin_box_decrement_surface"
            anchors.fill: parent
            anchors.margins: control.theme.scaledSize(4)
            theme: control.theme
            radius: Math.max(0, control.theme.controlRadius - control.theme.scaledSize(2))
            hovered: decrementTarget.enabled && control.down.hovered
            down: decrementTarget.enabled && control.down.pressed
            Text {
                anchors.centerIn: parent
                text: "−"
                color: decrementTarget.enabled ? control.theme.text : control.theme.muted
                font.pixelSize: control.theme.scaledSize(18)
            }
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
