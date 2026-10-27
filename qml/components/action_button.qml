import QtQuick
import QtQuick.Controls
import QuickClash.Ui

Button {
    id: control
    required property ThemeTokens theme
    property bool prominent: false
    property bool selected: false
    property bool destructive: false
    property bool ghost: false
    property bool pending: false
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
    activeFocusOnTab: true
    hoverEnabled: true
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.foreground()
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: StateSurface {
        theme: control.theme
        focused: control.activeFocus
        color: control.pending ? theme.accentSurface : !control.enabled ? theme.inset
             : control.destructive ? (control.down ? theme.errorPressed : control.hovered ? theme.errorHover : theme.errorSurface)
             : control.prominent ? (control.down ? theme.primaryPressed : control.hovered ? theme.primaryHover : theme.accent)
             : control.selected ? theme.stateColor(true, control.hovered, control.down)
             : control.down ? theme.pressed : control.hovered ? theme.hover : control.ghost ? "transparent" : theme.inset
    }
}
