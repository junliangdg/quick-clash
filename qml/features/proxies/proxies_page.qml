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
    // O(1) lookups for per-node delegate bindings instead of linear scans.
    readonly property var nodeByName: {
        const index = Object.create(null);
        for (let i = 0; i < nodes.length; ++i)
            index[nodes[i].name] = nodes[i];
        return index;
    }
    readonly property var groupNames: {
        const names = Object.create(null);
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
        // QStringList arrives as a QML sequence, which is not a JavaScript Array.
        const hasSaved = saved !== undefined && saved !== null && typeof saved.length === "number";
        const remembered = hasSaved ? Array.from(saved).filter(name => typeof name === "string") : [];
        const policy = (runtime.outbound || {}).policy;
        expandedGroups = hasSaved ? remembered : policy ? [policy] : [];
    }
    function toggleGroup(name): void {
        expandedGroups = expandedGroups.includes(name) ? expandedGroups.filter(entry => entry !== name) : expandedGroups.concat([name]);
        ui.saveState(stateKey, {
            groups: expandedGroups
        });
    }
    function delayText(name): string {
        const result = delays[name];
        return result === "Queued" ? qsTr("Queued") : result === "Testing" ? qsTr("Testing…") : result === "Timeout" ? qsTr("Timed out") : result === "Failed" ? qsTr("Unavailable") : result === "Cancelled" ? qsTr("Cancelled") : result === "Stale" ? qsTr("Expired") : result || "—";
    }
    function delayColor(name): color {
        const result = delays[name];
        if (result === "Failed" || result === "Timeout")
            return theme.error;
        if (result === "Testing")
            return theme.warning;
        const value = parseInt(result);
        if (isNaN(value))
            return theme.muted;
        return value < 300 ? theme.success : value < 800 ? theme.warning : theme.error;
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
    ReplayList {
        anchors.fill: parent
        visible: root.ui.replay
        ui: root.ui
        theme: root.theme
        pageId: "proxies"
        stream: "proxies"
        replayModel: root.ui.proxyModel
    }
    Flickable {
        id: groupList
        visible: !root.ui.replay
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
                    border.color: root.theme.border
                    ColumnLayout {
                        id: groupContent
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10
                        ItemDelegate {
                            id: groupToggle
                            objectName: "proxy_group_" + groupTile.modelData.name
                            Layout.fillWidth: true
                            implicitHeight: root.theme.scaledSize(54)
                            activeFocusOnTab: true
                            Accessible.name: groupTile.modelData.name
                            onClicked: root.toggleGroup(groupTile.modelData.name)
                            background: StateSurface {
                                theme: root.theme
                                hovered: groupToggle.hovered
                                down: groupToggle.down
                                focused: groupToggle.activeFocus
                            }
                            contentItem: RowLayout {
                                spacing: 12
                                Label {
                                    text: groupTile.expanded ? "▾" : "▸"
                                    color: root.theme.accent
                                    font.pixelSize: root.theme.scaledSize(18)
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 2
                                    Label {
                                        text: groupTile.modelData.name
                                        color: root.theme.text
                                        font.weight: Font.DemiBold
                                        font.pixelSize: root.theme.scaledSize(16)
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
                                    visible: !!groupTile.modelData.current && groupToggle.width >= root.theme.scaledSize(420)
                                    text: groupTile.modelData.current || ""
                                    color: root.theme.accent
                                    Layout.maximumWidth: Math.max(80, groupToggle.width * 0.38)
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                }
                                // Group latency batch test lives on the group card header.
                                ActionButton {
                                    id: testGroup
                                    objectName: "test_group"
                                    theme: root.theme
                                    text: "⚡"
                                    implicitWidth: root.theme.scaledSize(40)
                                    implicitHeight: root.theme.compactHeight
                                    font.pixelSize: root.theme.scaledSize(16)
                                    Accessible.name: qsTr("Test this group")
                                    ThemedToolTip { theme: root.theme; visible: testGroup.hovered; text: qsTr("Test this group") }
                                    enabled: !!root.runtime.ready && !root.runtime.busy && !root.testing
                                    onClicked: root.ui.runtimeCommand("delay_batch", [groupTile.modelData.name])
                                }
                            }
                        }
                        Label {
                            visible: !!groupTile.modelData.current && groupToggle.width < root.theme.scaledSize(420)
                            text: groupTile.modelData.current || ""
                            color: root.theme.accent
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                        RowLayout {
                            // Always visible while expanded so starting or finishing
                            // tests never changes the card height.
                            visible: groupTile.expanded
                            Layout.fillWidth: true
                            spacing: 8
                            Layout.minimumHeight: root.theme.compactHeight
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
                                implicitHeight: root.theme.compactHeight
                                onClicked: root.ui.runtimeCommand("cancel_delays")
                            }
                        }
                        // Reserve the full grid extent, but only create visible rows
                        // and one extra row on either side. There is no nested scroller.
                        Item {
                            id: nodeGrid
                            objectName: "proxy_node_grid"
                            readonly property var members: groupTile.modelData.members || []
                            readonly property int rowHeight: root.theme.scaledSize(64) + 8
                            readonly property int rows: Math.ceil(members.length / groupTile.nodeColumns)
                            readonly property real pageY: groupTile.y + groupContent.y + y
                            readonly property int firstRow: Math.min(rows, Math.max(0, Math.floor((groupList.contentY - pageY) / rowHeight) - 1))
                            readonly property int endRow: Math.min(rows, Math.max(0, Math.ceil((groupList.contentY + groupList.height - pageY) / rowHeight) + 1))
                            readonly property int firstIndex: firstRow * groupTile.nodeColumns
                            readonly property int endIndex: Math.min(members.length, endRow * groupTile.nodeColumns)
                            function focusMember(member): void {
                                const target = Math.max(0, Math.min(members.length - 1, member));
                                const top = pageY + Math.floor(target / groupTile.nodeColumns) * rowHeight;
                                groupList.contentY = Math.max(0, Math.min(groupList.contentHeight - groupList.height,
                                    top < groupList.contentY ? top : top + rowHeight > groupList.contentY + groupList.height ? top + rowHeight - groupList.height : groupList.contentY));
                                Qt.callLater(() => {
                                    const item = nodeCards.itemAt(target - firstIndex);
                                    if (item)
                                        item.forceActiveFocus(Qt.TabFocusReason);
                                });
                            }
                            visible: groupTile.expanded && (groupTile.modelData.members || []).length > 0
                            Layout.fillWidth: true
                            implicitHeight: rows * rowHeight - (rows > 0 ? 8 : 0)
                            Repeater {
                                id: nodeCards
                                model: groupTile.expanded ? Math.max(0, nodeGrid.endIndex - nodeGrid.firstIndex) : 0
                                // Compact card: clicking selects; info, latency and the
                                // test action share one row.
                                ItemDelegate {
                                    id: nodeTile
                                    objectName: "proxy_member_" + groupTile.modelData.name + "_" + modelData
                                    required property int index
                                    readonly property int memberIndex: nodeGrid.firstIndex + index
                                    readonly property string modelData: nodeGrid.members[memberIndex] || ""
                                    readonly property bool current: groupTile.modelData.current === modelData
                                    readonly property bool testable: !(modelData in root.groupNames) && !["DIRECT", "REJECT"].includes(modelData)
                                    width: groupTile.nodeCardWidth
                                    height: root.theme.scaledSize(64)
                                    x: (memberIndex % groupTile.nodeColumns) * (width + 8)
                                    y: Math.floor(memberIndex / groupTile.nodeColumns) * nodeGrid.rowHeight
                                    // Compact single-line card: tight padding and one
                                    // name line keep the node grid dense.
                                    padding: 8
                                    enabled: !root.runtime.busy || !!root.runtime.selectingProxy
                                    activeFocusOnTab: true
                                    Accessible.name: modelData
                                    ThemedToolTip { theme: root.theme; visible: nodeTile.hovered && !!groupTile.modelData.canSelect && !nodeTile.current; text: qsTr("Click to select") }
                                    onClicked: if (groupTile.modelData.canSelect && (!current || root.runtime.selectingProxy))
                                        root.ui.runtimeCommand("proxy", [groupTile.modelData.name, modelData])
                                    Keys.onUpPressed: nodeGrid.focusMember(memberIndex - groupTile.nodeColumns)
                                    Keys.onDownPressed: nodeGrid.focusMember(memberIndex + groupTile.nodeColumns)
                                    Keys.onLeftPressed: nodeGrid.focusMember(memberIndex - 1)
                                    Keys.onRightPressed: nodeGrid.focusMember(memberIndex + 1)
                                    Keys.onPressed: event => {
                                        if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                                            nodeGrid.focusMember(event.key === Qt.Key_Home ? 0 : nodeGrid.members.length - 1);
                                            event.accepted = true;
                                        }
                                    }
                                    background: StateSurface {
                                        theme: root.theme
                                        selected: nodeTile.current
                                        hovered: nodeTile.enabled && nodeTile.hovered
                                        down: nodeTile.enabled && nodeTile.down
                                        focused: nodeTile.activeFocus
                                        border.width: 1
                                        border.color: nodeTile.current ? root.theme.accent : root.theme.border
                                    }
                                    contentItem: ColumnLayout {
                                        spacing: 2
                                        Label {
                                            text: nodeTile.modelData
                                            color: nodeTile.current ? root.theme.onSelection : root.theme.text
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
                                                color: nodeTile.current ? root.theme.onSelection : root.theme.muted
                                                font.pixelSize: root.theme.supportingSize
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                                textFormat: Text.PlainText
                                            }
                                            Label {
                                                text: root.delayText(nodeTile.modelData)
                                                color: root.delayColor(nodeTile.modelData)
                                                font.weight: Font.DemiBold
                                                font.pixelSize: root.theme.supportingSize
                                            }
                                            ActionButton {
                                                id: testNode
                                                objectName: "test_node"
                                                theme: root.theme
                                                text: "⚡"
                                                implicitWidth: 30
                                                implicitHeight: root.theme.compactHeight
                                                leftPadding: 4
                                                rightPadding: 4
                                                font.pixelSize: root.theme.scaledSize(12)
                                                visible: nodeTile.testable
                                                enabled: !!root.runtime.ready && !root.runtime.busy && !root.testing
                                                Accessible.name: qsTr("Test delay")
                                                ThemedToolTip { theme: root.theme; visible: testNode.hovered; text: qsTr("Test delay") }
                                                onClicked: root.ui.runtimeCommand("delay", [nodeTile.modelData])
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
