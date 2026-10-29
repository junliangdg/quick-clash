import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

FlatTile {
    id: root
    required property string heading
    required property string description
    required property string milestone
    implicitHeight: column.implicitHeight + 40
    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        Label {
            font.family: root.theme.fontFamily
            text: root.heading
            color: root.theme.text
            font.pixelSize: root.theme.scaledSize(22)
            font.bold: true
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            font.family: root.theme.fontFamily
            text: root.description
            color: root.theme.muted
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            font.family: root.theme.fontFamily
            text: root.milestone
            color: root.theme.warning
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
