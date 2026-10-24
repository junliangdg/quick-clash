pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    FlatTile {
        width: parent.width
        theme: root.theme
        implicitHeight: content.implicitHeight + 40
        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14
            Label {
                text: qsTr("Website test")
                color: root.theme.text
                font.pixelSize: 20
                font.bold: true
            }
            TextField {
                id: target
                Layout.fillWidth: true
                placeholderText: qsTr("https://your-website.example")
                maximumLength: 8192
                text: root.ui.runtime.testTarget || ""
            }
            Label {
                text: qsTr("Send an HTTPS HEAD request through the current local proxy. No direct fallback or automatic IP lookup.")
                color: root.theme.muted
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Run test")
                prominent: true
                enabled: !!root.ui.runtime.ready
                onClicked: root.ui.runtimeCommand("test", [target.text])
            }
            Label {
                text: root.ui.runtime.testResult || ""
                color: root.theme.text
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
    }
}
