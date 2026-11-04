pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

FlatTile {
    id: root
    property string title
    default property alias settingsContent: content.data
    implicitHeight: content.implicitHeight + 2 * theme.panelPadding
    ColumnLayout {
        id: content
        x: root.theme.panelPadding
        y: root.theme.panelPadding
        width: Math.max(0, root.width - 2 * root.theme.panelPadding)
        spacing: 0
        Label {
            text: root.title
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.bodySize
            font.weight: Font.DemiBold
            color: root.theme.text
            Layout.fillWidth: true
            Layout.bottomMargin: 8
            wrapMode: Text.WordWrap
        }
    }
}
