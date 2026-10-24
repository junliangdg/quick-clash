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
        anchors.margins: 18
        spacing: 10
        Label {
            font.family: root.theme.fontFamily
            text: qsTr("Primary proxy group")
            color: root.theme.text
            font.bold: true
        }
        Label {
            font.family: root.theme.fontFamily
            text: root.ui.replay ? qsTr("Example group → Sample node 0001") : ((root.ui.runtime.groups || []).length ? root.ui.runtime.groups[0].name + " → " + root.ui.runtime.groups[0].current : qsTr("No proxy groups available"))
            color: root.theme.muted
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Label {
            font.family: root.theme.fontFamily
            text: qsTr("View group policies and available selections on the proxies page.")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        ActionButton {
            theme: root.theme
            text: qsTr("View proxies")
            onClicked: root.ui.navigate("proxies", {
                "group": "primary"
            })
        }
    }
}
