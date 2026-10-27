import QtQuick
import QtQuick.Controls
import QuickClash.Ui

TextArea {
    id: control
    required property ThemeTokens theme
    property bool invalid: false
    font.family: theme.fontFamily
    font.pixelSize: theme.bodySize
    padding: 12
    selectByMouse: true
    color: enabled ? theme.text : theme.muted
    placeholderTextColor: theme.muted
    selectionColor: theme.accent
    selectedTextColor: theme.accentForeground
    background: StateSurface {
        theme: control.theme
        color: control.enabled ? theme.surface : theme.inset
        border.width: 1
        border.color: control.invalid ? theme.error : theme.controlOutline
        focused: control.activeFocus
    }
}
