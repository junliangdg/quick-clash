pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    ReplayList {
        anchors.fill: parent
        visible: root.ui.replay
        ui: root.ui
        theme: root.theme
        pageId: "proxies"
        stream: "proxies"
        replayModel: ui.proxyModel
    }
    ScrollView {
        id: proxyScroll
        anchors.fill: parent
        visible: !root.ui.replay
        contentWidth: availableWidth
        ColumnLayout {
            width: proxyScroll.availableWidth
            spacing: 14
            Label {
                text: root.ui.runtime.ready ? qsTr("Proxy groups") : qsTr("Start the core to see proxy groups")
                color: root.theme.text
                font.pixelSize: 20
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Refresh")
                enabled: !!root.ui.runtime.ready
                onClicked: root.ui.runtimeCommand("refresh")
            }
            Repeater {
                model: root.ui.runtime.groups || []
                delegate: FlatTile {
                    id: groupTile
                    required property var modelData
                    Layout.fillWidth: true
                    theme: root.theme
                    implicitHeight: groupContent.implicitHeight + 40
                    ColumnLayout {
                        id: groupContent
                        anchors.fill: parent
                        anchors.margins: 20
                        Label {
                            text: groupTile.modelData.name + " · " + groupTile.modelData.current
                            color: root.theme.text
                            font.bold: true
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                        }
                        ComboBox {
                            id: member
                            Layout.fillWidth: true
                            visible: !!groupTile.modelData.canSelect
                            model: groupTile.modelData.members
                            currentIndex: Math.max(0, groupTile.modelData.members.indexOf(groupTile.modelData.current))
                        }
                        ActionButton {
                            theme: root.theme
                            text: qsTr("Select node")
                            visible: !!groupTile.modelData.canSelect
                            enabled: !root.ui.runtime.busy
                            onClicked: root.ui.runtimeCommand("proxy", [groupTile.modelData.name, member.currentText])
                        }
                        Label {
                            text: groupTile.modelData.type === "URLTest" ? qsTr("Automatic latency selection · url-test") : groupTile.modelData.type === "Fallback" ? qsTr("Automatic failover · fallback") : qsTr("Manual selection · select")
                            color: root.theme.muted
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            visible: !groupTile.modelData.canSelect
                            text: qsTr("Members: %1").arg(groupTile.modelData.members.join(" · "))
                            color: root.theme.muted
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }
    }
}
