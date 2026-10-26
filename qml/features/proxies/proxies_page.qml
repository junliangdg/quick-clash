pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    property string groupName: ""
    readonly property string stateKey: "proxies:" + (ui.runtime.selectedId || "")
    readonly property var groups: ui.groups.filter(group => group.name !== "GLOBAL" || (ui.runtime.preferences || {}).mode === "global" || ui.groups.length === 1)
    readonly property var group: groups.find(group => group.name === groupName) || groups.find(group => group.name === (ui.runtime.outbound || {}).policy) || groups[0] || ({})
    Component.onCompleted: {
        groupName = ui.readState(stateKey).group || "";
        ui.setDemand("proxies:page", "proxies", active);
    }
    onStateKeyChanged: groupName = ui.readState(stateKey).group || ""
    readonly property var progress: (ui.runtime.features || {}).delayTest || ({})
    readonly property bool testing: progress.state === "Running"
    readonly property bool active: ui.uiVisible && visible && !ui.replay
    onActiveChanged: ui.setDemand("proxies:page", "proxies", active)
    Component.onDestruction: ui.setDemand("proxies:page", "proxies", false)
    function delayText(name): string {
        const result = ui.delays[name];
        return result === "Testing" ? qsTr("Testing…") : result === "Failed" ? qsTr("Unavailable") : result === "Cancelled" ? qsTr("Cancelled") : result || "—";
    }
    ReplayList {
        anchors.fill: parent
        visible: root.ui.replay
        ui: root.ui
        theme: root.theme
        pageId: "proxies"
        stream: "proxies"
        replayModel: ui.proxyModel
    }
    ColumnLayout {
        anchors.fill: parent
        visible: !root.ui.replay
        spacing: 10
        Label {
            text: root.ui.runtime.selectedName ? qsTr("Proxy groups · %1").arg(root.ui.runtime.selectedName) : qsTr("Select a subscription to see nodes")
            color: root.theme.text
            font.pixelSize: 20
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            ActionButton { theme: root.theme; text: qsTr("Manage subscriptions"); onClicked: root.ui.navigate("subscriptions") }
            ActionButton { theme: root.theme; text: qsTr("Refresh"); enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy; onClicked: root.ui.runtimeCommand("refresh") }
        }
        Label {
            text: root.ui.runtimeError
            visible: text.length > 0
            color: root.theme.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        OutboundStatus { ui: root.ui; theme: root.theme; Layout.fillWidth: true }
        Label {
            text: qsTr("Rules can use different groups. Viewing a group does not change routing.")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label { text: qsTr("Viewing group"); color: root.theme.muted }
        ComboBox {
            id: groupSelector
            Layout.fillWidth: true
            model: root.groups
            textRole: "name"
            currentIndex: Math.max(0, root.groups.findIndex(group => group.name === root.group.name))
            onActivated: {
                root.groupName = root.groups[currentIndex].name;
                root.ui.saveState(root.stateKey, {group: root.groupName});
            }
        }
        Label {
            text: (root.group.current || "") + " · " + (["URLTest", "url-test"].includes(root.group.type) ? qsTr("Automatic latency selection · url-test") : ["Fallback", "fallback"].includes(root.group.type) ? qsTr("Automatic failover · fallback") : ["LoadBalance", "load-balance"].includes(root.group.type) ? qsTr("Load balancing · load-balance") : qsTr("Manual selection · select"))
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }
        TextField {
            id: delayTarget
            Layout.fillWidth: true
            text: root.ui.runtime.testTarget || "https://www.gstatic.com/generate_204"
            placeholderText: qsTr("HTTPS target for node delay")
            maximumLength: 8192
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            ActionButton {
                theme: root.theme
                text: qsTr("Test this group")
                enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy && !root.testing && !!root.group.name
                onClicked: root.ui.runtimeCommand("delay_batch", [root.group.name, delayTarget.text])
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Test all nodes")
                enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy && !root.testing
                onClicked: root.ui.runtimeCommand("delay_batch", ["", delayTarget.text])
            }
            ActionButton { theme: root.theme; text: qsTr("Cancel tests"); visible: root.testing; onClicked: root.ui.runtimeCommand("cancel_delays") }
        }
        Label {
            visible: !!root.progress.state
            text: (root.progress.state === "Cancelled" ? qsTr("Cancelled") : root.progress.state === "Completed" ? qsTr("Completed") : qsTr("Testing…")) + " · " + qsTr("%1 / %2 tested · %3 unavailable").arg(root.progress.completed || 0).arg(root.progress.total || 0).arg(root.progress.failed || 0)
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        TextField { id: nodeFilter; Layout.fillWidth: true; placeholderText: qsTr("Filter nodes by name"); maximumLength: 128 }
        ListView {
            id: nodes
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            reuseItems: true
            spacing: 8
            model: (root.group.members || []).filter(name => !nodeFilter.text || name.toLowerCase().includes(nodeFilter.text.trim().toLowerCase()))
            ScrollBar.vertical: ScrollBar {}
            delegate: FlatTile {
                id: nodeTile
                required property string modelData
                theme: root.theme
                width: nodes.width
                implicitHeight: nodeContent.implicitHeight + 24
                border.width: root.group.current === modelData ? 1 : 0
                border.color: root.theme.accent
                ColumnLayout {
                    id: nodeContent
                    anchors.fill: parent
                    anchors.margins: 12
                    Label { text: nodeTile.modelData; color: root.theme.text; font.bold: root.group.current === nodeTile.modelData; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText }
                    RowLayout {
                        Layout.fillWidth: true
                        ActionButton {
                            theme: root.theme
                            text: root.group.current === nodeTile.modelData ? qsTr("Selected") : qsTr("Select node")
                            visible: !!root.group.canSelect
                            enabled: !root.ui.runtime.busy && root.group.current !== nodeTile.modelData
                            onClicked: root.ui.runtimeCommand("proxy", [root.group.name, nodeTile.modelData])
                        }
                        Label { text: root.delayText(nodeTile.modelData); color: root.theme.muted; Layout.fillWidth: true; elide: Text.ElideRight }
                        ActionButton {
                            theme: root.theme
                            text: qsTr("Test delay")
                            enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy && !root.testing
                            onClicked: root.ui.runtimeCommand("delay", [nodeTile.modelData, delayTarget.text])
                        }
                    }
                }
            }
            Label { visible: nodes.count === 0; text: qsTr("No nodes in this group"); color: root.theme.muted; width: parent.width; wrapMode: Text.WordWrap }
        }
    }
}
