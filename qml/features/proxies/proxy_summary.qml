import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

FlatTile {
    id: root
    required property UiState ui
    required property string instanceKey
    property bool active: ui.uiVisible && visible
    function syncDemand(): void {
        ui.setDemand(instanceKey, "proxies", active);
    }
    onActiveChanged: syncDemand()
    Component.onCompleted: syncDemand()
    Component.onDestruction: ui.setDemand(instanceKey, "proxies", false)
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.theme.panelPadding
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label {
                font.family: root.theme.fontFamily
                text: qsTr("Current outbound")
                color: root.theme.text
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            ActionButton {
                theme: root.theme
                text: qsTr("View proxies")
                ghost: true
                onClicked: root.ui.navigate("proxies", {
                    "group": "primary"
                })
            }
        }
        OutboundStatus {
            ui: root.ui
            theme: root.theme
            compact: true
            Layout.fillWidth: true
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Label {
            font.family: root.theme.fontFamily
            text: qsTr("New connections follow this policy. Other rules and existing connections may use different nodes.")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }
}
