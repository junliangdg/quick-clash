import QtQuick
import QtQuick.Controls
import QuickClash.Ui

Label {
    required property ThemeTokens theme
    property string result: ""
    property bool stale: false
    color: stale ? theme.muted : theme.delayColor(result)
    font.family: theme.fontFamily
    font.pixelSize: theme.supportingSize
    font.weight: Font.DemiBold
    textFormat: Text.PlainText
    maximumLineCount: 1
}
