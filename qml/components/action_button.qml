import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QuickClash.Ui

Button {
    id: control
    required property ThemeTokens theme
    Accessible.name: text
    property bool prominent: false
    property bool selected: false
    property bool destructive: false
    property bool ghost: false
    property bool pending: false
    readonly property color foregroundColor: foreground()
    function foreground(): color {
        if (pending)
            return theme.onSelection;
        if (!enabled)
            return theme.muted;
        if (destructive)
            return theme.error;
        if (prominent)
            return theme.accentForeground;
        if (selected)
            return theme.onSelection;
        if (ghost)
            return theme.accent;
        return theme.text;
    }
    font.family: theme.fontFamily
    font.pixelSize: theme.bodySize
    font.weight: prominent || selected ? Font.DemiBold : Font.Normal
    implicitHeight: theme.controlHeight
    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding, 36)
    leftPadding: 12
    rightPadding: 12
    spacing: 8
    icon.width: theme.scaledSize(16)
    icon.height: theme.scaledSize(16)
    icon.color: foregroundColor
    activeFocusOnTab: true
    Keys.onReturnPressed: event => {
        if (!event.isAutoRepeat)
            control.click();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        if (!event.isAutoRepeat)
            control.click();
        event.accepted = true;
    }
    hoverEnabled: true
    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: control.foregroundColor
    }
    background: StateSurface {
        theme: control.theme
        focused: control.activeFocus
        color: control.pending ? theme.accentSurface : !control.enabled ? theme.inset : control.destructive ? (control.down ? theme.errorPressed : control.hovered ? theme.errorHover : theme.errorSurface) : control.prominent ? (control.down ? theme.primaryPressed : control.hovered ? theme.primaryHover : theme.accent) : control.selected ? theme.stateColor(true, control.hovered, control.down) : control.down ? theme.pressed : control.hovered ? theme.hover : control.ghost ? "transparent" : theme.inset
    }
}
