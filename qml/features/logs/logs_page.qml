pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ScrollView {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    contentWidth: availableWidth
    clip: true
    ColumnLayout {
        width: root.availableWidth
        spacing: 12
        Label {
            text: qsTr("Runtime errors")
            color: root.theme.text
            font.pixelSize: 20
            font.bold: true
        }
        Label {
            text: qsTr("The latest 100 errors contain timestamps and error codes. Full core logs are planned for a later release.")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        ActionButton {
            theme: root.theme
            text: qsTr("Open repair settings")
            onClicked: root.ui.navigate("settings")
        }
        Label {
            visible: !(root.ui.runtime.errors || []).length
            text: qsTr("No errors recorded")
            color: root.theme.muted
        }
        Repeater {
            model: root.ui.runtime.errors || []
            delegate: FlatTile {
                id: entry
                required property var modelData
                theme: root.theme
                Layout.fillWidth: true
                implicitHeight: 58
                Label {
                    anchors.fill: parent
                    anchors.margins: 14
                    text: entry.modelData.time + " · " + entry.modelData.code
                    color: root.theme.text
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
