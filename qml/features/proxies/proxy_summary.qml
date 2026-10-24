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
            text: qsTr("主代理组")
            color: root.theme.text
            font.bold: true
        }
        Label {
            font.family: root.theme.fontFamily
            text: root.ui.replay ? qsTr("演示组 → 示例节点 0001") : qsTr("暂无可用代理组")
            color: root.theme.muted
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Label {
            font.family: root.theme.fontFamily
            text: qsTr("摘要独立嵌入，不创建完整代理页")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        ActionButton {
            theme: root.theme
            text: qsTr("查看代理")
            onClicked: root.ui.navigate("proxies", {
                "group": "primary"
            })
        }
    }
}
