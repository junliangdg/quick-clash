pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    property var expandedGroups: []
    readonly property string stateKey: "proxies:" + (ui.runtime.selectedId || "")
    // Channel values are read once per change notification; delegates never
    // re-materialize the whole channel JSON on every binding evaluation.
    readonly property var runtime: ui.runtime
    readonly property var groups: ui.groups.filter(group => group.name !== "GLOBAL" || (runtime.preferences || {}).mode === "global" || ui.groups.length === 1)
    readonly property var nodes: ui.nodes
    readonly property var delays: ui.delays
    readonly property var progress: (runtime.features || {}).delayTest || ({})
    readonly property bool testing: progress.state === "Running"
    readonly property bool active: ui.uiVisible && visible && !ui.replay
    readonly property string delayTarget: runtime.delayTarget || "https://www.gstatic.com/generate_204"
    // O(1) lookups for per-node delegate bindings instead of linear scans.
    readonly property var nodeByName: {
        const index = {};
        for (let i = 0; i < nodes.length; ++i)
            index[nodes[i].name] = nodes[i];
        return index;
    }
    readonly property var groupNames: {
        const names = {};
        for (let i = 0; i < groups.length; ++i)
            names[groups[i].name] = true;
        return names;
    }
    Component.onCompleted: {
        restoreExpandedGroups();
        ui.setDemand("proxies:page", "proxies", active);
    }
    onStateKeyChanged: restoreExpandedGroups()
    onActiveChanged: ui.setDemand("proxies:page", "proxies", active)
    Component.onDestruction: ui.setDemand("proxies:page", "proxies", false)
    // Groups expand independently; the remembered set is per subscription.
    function restoreExpandedGroups(): void {
        const saved = ui.readState(stateKey).groups;
        const remembered = Array.isArray(saved) ? saved.filter(name => typeof name === "string") : [];
        const policy = (runtime.outbound || {}).policy;
        expandedGroups = remembered.length > 0 ? remembered : policy ? [policy] : [];
    }
    function toggleGroup(name): void {
        expandedGroups = expandedGroups.includes(name) ? expandedGroups.filter(entry => entry !== name) : expandedGroups.concat([name]);
        ui.saveState(stateKey, {
            groups: expandedGroups
        });
    }
    function delayText(name): string {
        const result = delays[name];
        return result === "Testing" ? qsTr("Testing…") : result === "Failed" ? qsTr("Unavailable") : result === "Cancelled" ? qsTr("Cancelled") : result || "—";
    }
    function delayColor(name): color {
        const result = delays[name];
        if (result === "Failed")
            return theme.dark ? "#ff8a80" : "#c8392f";
        if (result === "Testing")
            return theme.warning;
        const value = parseInt(result);
        if (isNaN(value))
            return theme.muted;
        return value < 300 ? theme.success : value < 800 ? theme.warning : theme.dark ? "#ff8a80" : "#c8392f";
    }
    function nodeDetails(name): string {
        const node = nodeByName[name];
        if (node)
            return (node.type || "") + (node.udp ? " · UDP" : "");
        const group = groups.find(entry => entry.name === name);
        return group ? group.type || "" : "";
    }
    function groupType(group): string {
        return ["URLTest", "url-test"].includes(group.type) ? qsTr("Automatic latency selection · url-test") : ["Fallback", "fallback"].includes(group.type) ? qsTr("Automatic failover · fallback") : ["LoadBalance", "load-balance"].includes(group.type) ? qsTr("Load balancing · load-balance") : qsTr("Manual selection · select");
    }
    // Group cards are few and laid out directly; only the expanded group
    // instantiates its node grid. The page keeps a single scrollbar whose
    // length derives from the real layout, so recycled variable-height rows
    // cannot make the content height (and the scrollbar) jump while scrolling.
    Flickable {
        id: groupList
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: groupsColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: groupsColumn
            width: groupList.width
            spacing: 12
            Label {
                visible: root.groups.length === 0
                text: qsTr("Select a subscription to see nodes")
                color: root.theme.muted
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: root.groups
                FlatTile {
                    id: groupTile
                    required property var modelData
                    readonly property bool expanded: root.expandedGroups.includes(modelData.name)
                    readonly property int nodeColumns: Math.max(1, Math.floor((width - 36) / 232))
                    readonly property real nodeCardWidth: (width - 28 - 8 * (nodeColumns - 1)) / nodeColumns
                    theme: root.theme
                    Layout.fillWidth: true
                    implicitHeight: groupContent.implicitHeight + 28
                    border.width: expanded ? 1 : 0
                    border.color: root.theme.accent
                    ColumnLayout {
                        id: groupContent
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10
                        ItemDelegate {
                            id: groupToggle
                            Layout.fillWidth: true
                            implicitHeight: 54
                            activeFocusOnTab: true
                            Accessible.name: groupTile.modelData.name
                            onClicked: root.toggleGroup(groupTile.modelData.name)
                            background: Rectangle {
                                radius: 12
                                color: groupTile.expanded || groupToggle.hovered ? root.theme.accentSurface : "transparent"
                                border.width: groupToggle.activeFocus ? 1 : 0
                                border.color: root.theme.accent
                            }
                            contentItem: RowLayout {
                                spacing: 12
                                Label {
                                    text: groupTile.expanded ? "▾" : "▸"
                                    color: root.theme.accent
                                    font.pixelSize: 18
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 2
                                    Label {
                                        text: groupTile.modelData.name
                                        color: root.theme.text
                                        font.bold: true
                                        font.pixelSize: 16
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        textFormat: Text.PlainText
                                    }
                                    Label {
                                        text: root.groupType(groupTile.modelData) + " · " + qsTr("%1 members").arg((groupTile.modelData.members || []).length)
                                        color: root.theme.muted
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                }
                                Label {
                                    visible: !!groupTile.modelData.current
                                    text: groupTile.modelData.current || ""
                                    color: root.theme.accent
                                    Layout.maximumWidth: Math.max(80, groupToggle.width * 0.38)
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                }
                                // Group latency batch test lives on the group card header.
                                ActionButton {
                                    objectName: "test_group"
                                    theme: root.theme
                                    text: "⚡"
                                    implicitWidth: 40
                                    implicitHeight: 32
                                    font.pixelSize: 16
                                    Accessible.name: qsTr("Test this group")
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Test this group")
                                    enabled: !!root.runtime.ready && !root.runtime.busy && !root.testing
                                    onClicked: root.ui.runtimeCommand("delay_batch", [groupTile.modelData.name, root.delayTarget])
                                }
                            }
                        }
                        RowLayout {
                            // Always visible while expanded so starting or finishing
                            // tests never changes the card height.
                            visible: groupTile.expanded
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                id: progressLabel
                                text: root.progress.state ? (root.progress.state === "Cancelled" ? qsTr("Cancelled") : root.progress.state === "Completed" ? qsTr("Completed") : qsTr("Testing…")) + " · " + qsTr("%1 / %2 tested · %3 unavailable").arg(root.progress.completed || 0).arg(root.progress.total || 0).arg(root.progress.failed || 0) : qsTr("Not tested")
                                color: root.theme.muted
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            ActionButton {
                                theme: root.theme
                                text: qsTr("Cancel")
                                visible: root.testing
                                // Match the row text height so showing or hiding the
                                // button never changes the row height.
                                implicitHeight: progressLabel.implicitHeight
                                onClicked: root.ui.runtimeCommand("cancel_delays")
                            }
                        }
                        // Single page-level scrollbar: nodes are fully laid out in the
                        // group card and never scroll internally.
                        Flow {
                            visible: groupTile.expanded && (groupTile.modelData.members || []).length > 0
                            Layout.fillWidth: true
                            spacing: 8
                            Repeater {
                                // Only the expanded group instantiates node cards;
                                // collapsing the group releases them.
                                model: groupTile.expanded ? (groupTile.modelData.members || []) : []
                                // Compact card: clicking selects; info, latency and the
                                // test action share one row.
                                ItemDelegate {
                                    id: nodeTile
                                    required property string modelData
                                    readonly property bool current: groupTile.modelData.current === modelData
                                    readonly property bool testable: !(modelData in root.groupNames) && !["DIRECT", "REJECT"].includes(modelData)
                                    width: groupTile.nodeCardWidth
                                    // Compact single-line card: tight padding and one
                                    // name line keep the node grid dense.
                                    padding: 8
                                    enabled: !root.runtime.busy
                                    activeFocusOnTab: true
                                    Accessible.name: modelData
                                    ToolTip.visible: hovered && !!groupTile.modelData.canSelect && !current
                                    ToolTip.text: qsTr("Click to select")
                                    onClicked: if (groupTile.modelData.canSelect && !current)
                                        root.ui.runtimeCommand("proxy", [groupTile.modelData.name, modelData])
                                    background: Rectangle {
                                        radius: 12
                                        color: nodeTile.current || nodeTile.hovered ? root.theme.accentSurface : root.theme.inset
                                        border.width: nodeTile.current || nodeTile.activeFocus ? 1 : 0
                                        border.color: root.theme.accent
                                        opacity: nodeTile.enabled ? 1 : 0.6
                                    }
                                    contentItem: ColumnLayout {
                                        spacing: 2
                                        Label {
                                            text: nodeTile.modelData
                                            color: root.theme.text
                                            font.bold: nodeTile.current
                                            Layout.fillWidth: true
                                            maximumLineCount: 1
                                            elide: Text.ElideRight
                                            textFormat: Text.PlainText
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Label {
                                                text: root.nodeDetails(nodeTile.modelData)
                                                color: root.theme.muted
                                                font.pixelSize: 11
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                                textFormat: Text.PlainText
                                            }
                                            Label {
                                                text: root.delayText(nodeTile.modelData)
                                                color: root.delayColor(nodeTile.modelData)
                                                font.bold: true
                                                font.pixelSize: 11
                                            }
                                            ActionButton {
                                                objectName: "test_node"
                                                theme: root.theme
                                                text: "⚡"
                                                implicitWidth: 30
                                                implicitHeight: 24
                                                leftPadding: 4
                                                rightPadding: 4
                                                font.pixelSize: 12
                                                visible: nodeTile.testable
                                                enabled: !!root.runtime.ready && !root.runtime.busy && !root.testing
                                                Accessible.name: qsTr("Test delay")
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Test delay")
                                                onClicked: root.ui.runtimeCommand("delay", [nodeTile.modelData, root.delayTarget])
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        Label {
                            visible: groupTile.expanded && (groupTile.modelData.members || []).length === 0
                            text: qsTr("No nodes in this group")
                            color: root.theme.muted
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
