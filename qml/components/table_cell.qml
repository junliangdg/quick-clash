import QtQuick
import QtQuick.Controls
import QuickClash.Ui

Item {
    id: root
    required property ThemeTokens theme
    property string text: ""
    property string supporting: ""
    property bool numeric: false
    property bool heading: false
    property color foreground: theme.text
    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: 10
        Label {
            width: parent.width
            text: root.text
            textFormat: Text.PlainText
            color: root.heading ? root.theme.muted : root.foreground
            font.family: root.theme.fontFamily
            font.pixelSize: root.heading ? root.theme.supportingSize : root.theme.bodySize
            horizontalAlignment: root.numeric ? Text.AlignRight : Text.AlignLeft
            elide: Text.ElideRight
        }
        Label {
            visible: root.supporting.length > 0
            width: parent.width
            text: root.supporting
            textFormat: Text.PlainText
            color: root.theme.muted
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.supportingSize
            horizontalAlignment: root.numeric ? Text.AlignRight : Text.AlignLeft
            elide: Text.ElideRight
        }
    }
}
