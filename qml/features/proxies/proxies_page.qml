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
    readonly property var stateRevision: ui.proxyStateRevision
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
        ui.setDemand("custom_groups:page", "custom_groups", active);
    }
    onStateKeyChanged: restoreExpandedGroups()
    onActiveChanged: {
        ui.setDemand("proxies:page", "proxies", active);
        ui.setDemand("custom_groups:page", "custom_groups", active);
    }
    Component.onDestruction: {
        ui.setDemand("proxies:page", "proxies", false);
        ui.setDemand("custom_groups:page", "custom_groups", false);
    }
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
    function isAutomatic(group): bool {
        return ["URLTest", "url-test", "Fallback", "fallback"].includes(group.type);
    }
    function groupCurrentText(group): string {
        if (group.current)
            return group.current;
        if (!root.isAutomatic(group))
            return "";
        return root.runtime.ready ? qsTr("Awaiting automatic selection") : qsTr("Waiting for core confirmation");
    }
    function groupSourceText(group): string {
        const sources = group.sources || [];
        return sources.length > 0 ? qsTr("Provider: %1").arg(sources.join(", ")) : "";
    }
    CustomGroupEditor {
        id: groupEditor
        parent: Overlay.overlay
        ui: root.ui
        theme: root.theme
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
        objectName: "proxy_group_list"
        visible: !root.ui.replay
        anchors.fill: parent
        anchors.rightMargin: -root.theme.scrollBarWidth
        clip: true
        contentWidth: width
        contentHeight: groupsColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            objectName: "proxy_page_bar"
            width: root.theme.scrollBarWidth
        }
        ColumnLayout {
            id: groupsColumn
            objectName: "proxy_content_column"
            width: root.width
            spacing: 12
            FlatTile {
                theme: root.theme
                Layout.fillWidth: true
                implicitHeight: customControls.implicitHeight + 24
                ColumnLayout {
                    id: customControls
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        ActionButton {
                            width: Math.min(implicitWidth, parent.width)
                            objectName: "new_custom_group"
                            theme: root.theme
                            text: qsTr("New strategy group")
                            enabled: !!root.runtime.selectedId && !root.runtime.busy && !!root.ui.customGroups.revision
                            onClicked: groupEditor.edit({})
                        }
                        Label {
                            objectName: "custom_group_hint"
                            text: qsTr("Local groups survive subscription updates")
                            color: root.theme.muted
                            width: Math.min(implicitWidth, parent.width)
                            height: Math.max(root.theme.controlHeight, implicitHeight)
                            wrapMode: Text.WordWrap
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Repeater {
                        model: root.ui.customGroups.definitions || []
                        Flow {
                            id: customRow
                            required property var modelData
                            readonly property var candidates: (root.ui.customGroups.groups || []).find(entry => entry.id === customRow.modelData.id) || ({})
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                text: qsTr("Local: %1").arg(customRow.modelData.name)
                                textFormat: Text.PlainText
                                color: root.theme.text
                                height: root.theme.controlHeight
                                verticalAlignment: Text.AlignVCenter
                                width: Math.min(260, customControls.width)
                                elide: Text.ElideRight
                            }
                            Label {
                                objectName: "custom_group_empty_warning"
                                visible: customRow.candidates.count === 0
                                text: qsTr("No matching nodes · traffic rejected")
                                color: root.theme.warning
                                height: Math.max(root.theme.controlHeight, implicitHeight)
                                verticalAlignment: Text.AlignVCenter
                                width: Math.min(260, customControls.width)
                                wrapMode: Text.WordWrap
                            }
                            ActionButton {
                                width: Math.min(implicitWidth, parent.width)
                                theme: root.theme
                                text: qsTr("Edit")
                                enabled: !root.runtime.busy
                                onClicked: groupEditor.edit(customRow.modelData)
                            }
                            ActionButton {
                                width: Math.min(implicitWidth, parent.width)
                                theme: root.theme
                                text: qsTr("Delete…")
                                enabled: !root.runtime.busy
                                onClicked: groupEditor.edit(customRow.modelData, true)
                            }
                        }
                    }
                    Label {
                        visible: !!root.ui.customGroups.error
                        text: root.ui.customGroups.error || ""
                        color: root.theme.warning
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }
            Label {
                visible: root.groups.length === 0
                text: qsTr("Select a subscription to see nodes")
                color: root.theme.muted
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                visible: !!(root.runtime.features || {}).groupsStale
                Layout.fillWidth: true
                spacing: 8
                Label {
                    text: qsTr("Group members may be out of date")
                    color: root.theme.warning
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                ActionButton {
                    theme: root.theme
                    text: qsTr("Refresh")
                    implicitHeight: root.theme.compactHeight
                    enabled: !!root.runtime.ready && !root.runtime.busy
                    onClicked: root.ui.runtimeCommand("refresh_groups", [])
                }
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
                            implicitHeight: headerContent.implicitHeight
                            padding: 0
                            activeFocusOnTab: true
                            Accessible.name: groupTile.modelData.name
                            onClicked: root.toggleGroup(groupTile.modelData.name)
                            background: StateSurface {
                                theme: root.theme
                                hovered: groupToggle.hovered
                                down: groupToggle.down
                                focused: groupToggle.activeFocus
                                insetFocus: true
                            }
                            contentItem: ColumnLayout {
                                id: headerContent
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Label {
                                        text: groupTile.expanded ? "▾" : "▸"
                                        color: root.theme.accent
                                        font.pixelSize: root.theme.scaledSize(18)
                                    }
                                    Label {
                                        id: groupNameLabel
                                        objectName: "proxy_group_title"
                                        text: groupTile.modelData.name
                                        color: root.theme.text
                                        font.weight: Font.DemiBold
                                        font.pixelSize: root.theme.scaledSize(16)
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        elide: Text.ElideRight
                                        textFormat: Text.PlainText
                                    }
                                    Label {
                                        visible: root.groupCurrentText(groupTile.modelData) !== "" && groupToggle.width >= root.theme.scaledSize(420)
                                        text: root.groupCurrentText(groupTile.modelData)
                                        color: groupTile.modelData.current ? root.theme.accent : root.theme.muted
                                        Layout.maximumWidth: Math.max(80, groupToggle.width * 0.35)
                                        elide: Text.ElideRight
                                        textFormat: Text.PlainText
                                    }
                                    ActionButton {
                                        id: testGroup
                                        objectName: "test_group"
                                        theme: root.theme
                                        icon.source: "qrc:/icons/dashboard/gauge.svg"
                                        text: groupToggle.width >= root.theme.scaledSize(420) ? qsTr("Test latency") : ""
                                        implicitHeight: root.theme.compactHeight
                                        Accessible.name: qsTr("Test delay for group %1").arg(groupTile.modelData.name)
                                        ThemedToolTip {
                                            theme: root.theme
                                            visible: testGroup.hovered || testGroup.activeFocus
                                            text: qsTr("Test this group")
                                        }
                                        enabled: !!root.runtime.ready && !root.runtime.busy && !root.testing
                                        onClicked: root.ui.runtimeCommand("delay_batch", [groupTile.modelData.name])
                                    }
                                }
                                Label {
                                    objectName: "proxy_group_subtitle"
                                    text: root.groupType(groupTile.modelData) + " · " + qsTr("%1 members").arg((groupTile.modelData.members || []).length) + (root.groupSourceText(groupTile.modelData) ? " · " + root.groupSourceText(groupTile.modelData) : "")
                                    color: root.theme.muted
                                    Layout.fillWidth: true
                                    Layout.leftMargin: groupNameLabel.x
                                    Layout.minimumWidth: 0
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        Label {
                            visible: root.groupCurrentText(groupTile.modelData) !== "" && groupToggle.width < root.theme.scaledSize(420)
                            text: root.groupCurrentText(groupTile.modelData)
                            color: groupTile.modelData.current ? root.theme.accent : root.theme.muted
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
                            // A fixed control pool is mapped onto a ring of grid rows.
                            // Changing the visible range therefore rebinds at most the
                            // rows entering/leaving the viewport instead of destroying
                            // and rebuilding the complete visible delegate set.
                            readonly property int poolRows: Math.ceil(groupList.height / rowHeight) + 3
                            readonly property int poolSize: Math.max(0, poolRows * groupTile.nodeColumns)
                            readonly property bool viewportActive: groupTile.expanded && pageY < groupList.contentY + groupList.height + rowHeight && pageY + implicitHeight > groupList.contentY - rowHeight
                            property int rowShift: 0
                            property int lastFirstRow: 0
                            onFirstRowChanged: {
                                const delta = firstRow - lastFirstRow;
                                lastFirstRow = firstRow;
                                rowShift = Math.abs(delta) < poolRows ? ((rowShift - delta) % poolRows + poolRows) % poolRows : 0;
                            }
                            onPoolRowsChanged: {
                                rowShift = 0;
                                lastFirstRow = firstRow;
                            }
                            function focusMember(member): void {
                                const target = Math.max(0, Math.min(members.length - 1, member));
                                const top = pageY + Math.floor(target / groupTile.nodeColumns) * rowHeight;
                                groupList.contentY = Math.max(0, Math.min(groupList.contentHeight - groupList.height, top < groupList.contentY ? top : top + rowHeight > groupList.contentY + groupList.height ? top + rowHeight - groupList.height : groupList.contentY));
                                Qt.callLater(() => {
                                    for (let i = 0; i < nodeCards.count; ++i) {
                                        const item = nodeCards.itemAt(i);
                                        if (item && item.memberIndex === target) {
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                            return;
                                        }
                                    }
                                });
                            }
                            visible: groupTile.expanded && (groupTile.modelData.members || []).length > 0
                            Layout.fillWidth: true
                            implicitHeight: rows * rowHeight - (rows > 0 ? 8 : 0)
                            Repeater {
                                id: nodeCards
                                model: nodeGrid.viewportActive && nodeGrid.members.length > 0 ? Math.min(nodeGrid.members.length, nodeGrid.poolSize) : 0
                                // Compact card: clicking selects; info, latency and the
                                // test action share one row.
                                ItemDelegate {
                                    id: nodeTile
                                    objectName: "proxy_member_" + groupTile.modelData.name + "_" + modelData
                                    required property int index
                                    readonly property int slotRow: Math.floor(index / groupTile.nodeColumns)
                                    readonly property int memberIndex: (nodeGrid.firstRow + (slotRow + nodeGrid.rowShift) % nodeGrid.poolRows) * groupTile.nodeColumns + index % groupTile.nodeColumns
                                    readonly property string modelData: nodeGrid.members[memberIndex] || ""
                                    readonly property var proxyState: {
                                        const revision = root.stateRevision;
                                        return root.ui.proxyState(modelData, groupTile.modelData.name);
                                    }
                                    readonly property bool current: groupTile.modelData.current === modelData
                                    readonly property bool testable: !(modelData in root.groupNames) && !["DIRECT", "REJECT"].includes(modelData)
                                    readonly property bool inWindow: memberIndex >= nodeGrid.firstIndex && memberIndex < nodeGrid.endIndex
                                    visible: inWindow
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
                                        insetFocus: true
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
                                            DelayLabel {
                                                visible: !nodeTile.testable
                                                theme: root.theme
                                                text: nodeTile.proxyState.text
                                                result: nodeTile.proxyState.result
                                                stale: !!(root.runtime.features || {}).groupsStale
                                                Accessible.description: nodeTile.proxyState.description
                                            }
                                            ActionButton {
                                                id: testNode
                                                objectName: "test_node"
                                                theme: root.theme
                                                text: nodeTile.proxyState.text
                                                implicitWidth: Math.max(root.theme.scaledSize(44), implicitContentWidth + leftPadding + rightPadding)
                                                implicitHeight: root.theme.compactHeight
                                                leftPadding: 6
                                                rightPadding: 6
                                                insetFocus: true
                                                visible: nodeTile.testable
                                                enabled: !!root.runtime.ready && !root.runtime.busy && !root.testing
                                                Accessible.name: qsTr("Test delay for %1").arg(nodeTile.modelData)
                                                Accessible.description: nodeTile.proxyState.description
                                                background: StateSurface {
                                                    theme: root.theme
                                                    hovered: testNode.enabled && testNode.hovered
                                                    down: testNode.enabled && testNode.down
                                                    focused: testNode.activeFocus
                                                    insetFocus: true
                                                }
                                                contentItem: DelayLabel {
                                                    theme: root.theme
                                                    text: testNode.text
                                                    result: nodeTile.proxyState.result
                                                    stale: !!(root.runtime.features || {}).groupsStale
                                                    horizontalAlignment: Text.AlignHCenter
                                                    verticalAlignment: Text.AlignVCenter
                                                }
                                                HoverHandler {
                                                    enabled: testNode.enabled
                                                    cursorShape: Qt.PointingHandCursor
                                                }
                                                ThemedToolTip {
                                                    theme: root.theme
                                                    visible: testNode.hovered || testNode.activeFocus
                                                    text: qsTr("Test delay")
                                                }
                                                onClicked: root.ui.runtimeCommand("delay", [nodeTile.modelData, groupTile.modelData.name])
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
