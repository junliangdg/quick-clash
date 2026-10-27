import QtQuick
import QuickClash.Ui

Rectangle {
    required property ThemeTokens theme
    radius: theme.panelRadius
    color: theme.surface
    border.width: 1
    border.color: theme.border
}
