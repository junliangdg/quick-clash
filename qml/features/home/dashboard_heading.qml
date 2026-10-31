pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

RowLayout {
    id: root
    required property ThemeTokens theme
    required property string text
    required property url source
    spacing: 10
    Rectangle {
        implicitWidth: root.theme.scaledSize(30)
        implicitHeight: root.theme.scaledSize(30)
        radius: root.theme.controlRadius
        color: root.theme.accentSurface
        UiIcon {
            anchors.centerIn: parent
            theme: root.theme
            source: root.source
            color: root.theme.onSelection
        }
    }
    Label {
        Layout.fillWidth: true
        text: root.text
        color: root.theme.text
        font.pixelSize: root.theme.bodySize
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
    }
}
