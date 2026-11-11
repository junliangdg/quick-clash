pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    objectName: "proxies_page"
    required property UiState ui
    required property ThemeTokens theme
    property var expandedGroups: []
    property string searchText: ""
    property bool restoringView: false
    property string locateGroup: ""
    property string locateMember: ""
    signal locateRequested(string group, string member)
    readonly property string stateKey: "proxies:" + (ui.runtime.selectedId || "")
    // Channel values are read once per change notification; delegates never
    // re-materialize the whole channel JSON on every binding evaluation.
    readonly property var runtime: ui.runtime
    readonly property var groups: ui.groups.filter(group => group.name !== "GLOBAL" || (runtime.preferences || {}).mode === "global" || ui.groups.length === 1)
    readonly property var nodes: ui.nodes
    readonly property string query: searchText.trim().toLocaleLowerCase()
    readonly property var matchingMembers: {
        const result = Object.create(null);
        for (const group of groups) {
            const matchesGroup = group.name.toLocaleLowerCase().includes(query);
            const members = group.members || [];
            result[group.name] = !query || matchesGroup ? members : members.filter(name => name.toLocaleLowerCase().includes(query));
        }
        return result;
    }
    readonly property int matchingCount: groups.reduce((count, group) => count + (matchingMembers[group.name] || []).length, 0)
    readonly property bool hasMatchingGroup: groups.some(group => group.name.toLocaleLowerCase().includes(query))
    readonly property string currentGroup: {
        if (!runtime.ready || (runtime.outbound || {}).balanced || (runtime.preferences || {}).mode === "direct")
            return "";
        const policy = (runtime.outbound || {}).policy;
        const preferred = groups.find(group => group.name === policy && canLocate(group));
        const fallback = groups.find(group => canLocate(group));
        return preferred ? preferred.name : fallback ? fallback.name : "";
    }
    readonly property var stateRevision: ui.proxyStateRevision
    readonly property var progress: (runtime.features || {}).delayTest || ({})
    readonly property bool testing: progress.state === "Running"
    // Selected cards already use accentSurface; lift the nested hover fill so it stays visible.
    readonly property color selectedDelayHover: Qt.tint(theme.accentSurface, theme.dark ? "#14FFFFFF" : "#80FFFFFF")
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
        for (const group of ui.groups)
            names[group.name] = true;
        return names;
    }
    readonly property bool progressOwnerVisible: groups.some(group => group.name === progress.group &&
        (query.length === 0 || group.name.toLocaleLowerCase().includes(query) || (matchingMembers[group.name] || []).length > 0) &&
        (query.length > 0 || expandedGroups.includes(group.name)))
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
        restoringView = true;
        const view = ui.readState(stateKey);
        const saved = view.groups;
        // QStringList arrives as a QML sequence, which is not a JavaScript Array.
        const hasSaved = saved !== undefined && saved !== null && typeof saved.length === "number";
        const remembered = hasSaved ? Array.from(saved).filter(name => typeof name === "string") : [];
        const policy = (runtime.outbound || {}).policy;
        expandedGroups = hasSaved ? remembered : policy ? [policy] : [];
        searchText = typeof view.filter === "string" ? view.filter : "";
        if (locateTimer)
            locateTimer.stop();
        restoringView = false;
    }
    function rememberView(): void {
        if (!restoringView)
            ui.saveState(stateKey, { groups: expandedGroups, filter: searchText });
    }
    onSearchTextChanged: {
        if (groupList)
            groupList.contentY = 0;
        rememberView();
    }
    function toggleGroup(name): void {
        expandedGroups = expandedGroups.includes(name) ? expandedGroups.filter(entry => entry !== name) : expandedGroups.concat([name]);
        rememberView();
    }
    function locateCurrent(name): void {
        const group = groups.find(entry => entry.name === name);
        if (!canLocate(group))
            return;
        searchText = "";
        if (!expandedGroups.includes(name))
            expandedGroups = expandedGroups.concat([name]);
        rememberView();
        locateGroup = name;
        locateMember = group.current;
        locateTimer.restart();
    }
    function canLocate(group): bool {
        return !!runtime.ready && !!group && !["LoadBalance", "load-balance"].includes(group.type) &&
            !!group.current && (group.members || []).includes(group.current);
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
    function editGroup(spec, remove = false): void {
        groupEditorLoader.active = true;
        const editor = groupEditorLoader.item as CustomGroupEditor;
        if (editor)
            editor.edit(spec, remove);
    }
    Loader {
        id: groupEditorLoader
        active: false
        sourceComponent: CustomGroupEditor {
            parent: Overlay.overlay
            ui: root.ui
            theme: root.theme
            onClosed: groupEditorLoader.active = false
        }
    }
    Timer {
        id: locateTimer
        interval: 16
        onTriggered: root.locateRequested(root.locateGroup, root.locateMember)
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
    GridLayout {
        id: searchTools
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: !root.ui.replay && root.groups.length > 0
        columns: width >= root.theme.scaledSize(620) ? 2 : 1
        columnSpacing: 8
        rowSpacing: 8
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: searchField.implicitHeight
            ThemedField {
                id: searchField
                objectName: "proxy_node_search"
                anchors.fill: parent
                theme: root.theme
                maximumLength: 128
                rightPadding: root.searchText.length > 0 ? clearSearch.width + 12 : 12
                placeholderText: qsTr("Search nodes or groups")
                Accessible.name: qsTr("Search nodes or groups")
                text: root.searchText
                onTextEdited: root.searchText = text
            }
            ActionButton {
                id: clearSearch
                objectName: "proxy_clear_search"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                theme: root.theme
                text: "×"
                Accessible.name: qsTr("Clear search")
                visible: root.searchText.length > 0
                ghost: true
                insetFocus: true
                width: root.theme.controlHeight
                height: root.theme.controlHeight
                onClicked: { root.searchText = ""; searchField.forceActiveFocus(); }
            }
        }
        ActionButton {
            objectName: "proxy_locate_current"
            theme: root.theme
            Layout.fillWidth: searchTools.columns === 1
            Layout.minimumWidth: 0
            text: qsTr("Locate current node")
            enabled: root.currentGroup.length > 0
            onClicked: root.locateCurrent(root.currentGroup)
        }
    }
    Flickable {
        id: groupList
        objectName: "proxy_group_list"
        visible: !root.ui.replay
        anchors.top: searchTools.visible ? searchTools.bottom : parent.top
        anchors.topMargin: searchTools.visible ? 12 : 0
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
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
            Label {
                objectName: "proxy_search_empty"
                visible: root.groups.length > 0 && root.query.length > 0 && root.matchingCount === 0 && !root.hasMatchingGroup
                text: qsTr("No matching nodes or groups")
                color: root.theme.muted
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                visible: root.testing && !root.progressOwnerVisible
                Layout.fillWidth: true
                Label {
                    text: root.progress.group ? qsTr("Testing group %1").arg(root.progress.group) : qsTr("Testing current node")
                    textFormat: Text.PlainText
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                ActionButton {
                    objectName: "proxy_active_test_cancel"
                    theme: root.theme
                    text: qsTr("Cancel")
                    onClicked: root.ui.runtimeCommand("cancel_delays")
                }
            }
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
                            prominent: true
                            enabled: !!root.runtime.selectedId && !root.runtime.busy && !!root.ui.customGroups.revision
                            Accessible.description: customGroupHint.text
                            onClicked: root.editGroup({})
                        }
                        Label {
                            id: customGroupHint
                            objectName: "custom_group_hint"
                            text: !root.runtime.selectedId ? qsTr("Select a subscription first") : root.runtime.busy ? qsTr("Wait for the current operation to finish") : !root.ui.customGroups.revision ? qsTr("Loading strategy groups…") : qsTr("Local groups survive subscription updates")
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
                                onClicked: root.editGroup(customRow.modelData)
                            }
                            ActionButton {
                                width: Math.min(implicitWidth, parent.width)
                                theme: root.theme
                                text: qsTr("Delete…")
                                enabled: !root.runtime.busy
                                onClicked: root.editGroup(customRow.modelData, true)
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
                    readonly property var viewMembers: root.matchingMembers[modelData.name] || []
                    readonly property bool expanded: root.query.length > 0 || root.expandedGroups.includes(modelData.name)
                    readonly property bool ownsProgress: root.progress.group === modelData.name
                    readonly property int nodeColumns: Math.max(1, Math.floor((width - 36) / 232))
                    readonly property real nodeCardWidth: (width - 28 - 8 * (nodeColumns - 1)) / nodeColumns
                    theme: root.theme
                    visible: root.query.length === 0 || modelData.name.toLocaleLowerCase().includes(root.query) || viewMembers.length > 0
                    Layout.fillWidth: true
                    implicitHeight: groupContent.implicitHeight + 28
                    border.color: root.theme.border
                    function locate(name): void {
                        const index = nodeGrid.members.indexOf(name);
                        if (index >= 0)
                            nodeGrid.focusMember(index);
                    }
                    Connections {
                        target: root
                        function onLocateRequested(group: string, member: string): void {
                            if (group === groupTile.modelData.name)
                                groupTile.locate(member);
                        }
                    }
                    ColumnLayout {
                        id: groupContent
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10
                        ItemDelegate {
                            id: groupToggle
                            objectName: "proxy_group_" + groupTile.modelData.name
                            Layout.fillWidth: true
                            implicitHeight: implicitContentHeight + topPadding + bottomPadding
                            padding: root.theme.focusExtent
                            activeFocusOnTab: true
                            Accessible.name: groupTile.modelData.name
                            onClicked: root.toggleGroup(groupTile.modelData.name)
                            background: StateSurface {
                                theme: root.theme
                                hovered: groupToggle.hovered && !testGroup.hovered
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
                                    UiIcon {
                                        theme: root.theme
                                        source: groupTile.expanded ? "qrc:/icons/dashboard/chevron_down.svg" : "qrc:/icons/dashboard/chevron_right.svg"
                                        color: root.theme.accent
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
                                        insetFocus: true
                                        background: StateSurface {
                                            theme: root.theme
                                            focused: testGroup.activeFocus
                                            insetFocus: true
                                            color: !testGroup.enabled ? root.theme.inset : testGroup.down ? root.theme.selectedHover : testGroup.hovered ? root.theme.accentSurface : root.theme.inset
                                        }
                                        HoverHandler {
                                            enabled: testGroup.enabled
                                            cursorShape: Qt.PointingHandCursor
                                        }
                                        Accessible.name: qsTr("Test delay for group %1").arg(groupTile.modelData.name)
                                        Accessible.description: qsTr("Includes subgroups")
                                        ThemedToolTip {
                                            theme: root.theme
                                            visible: testGroup.hovered || testGroup.activeFocus
                                            text: qsTr("Test group and subgroups")
                                        }
                                        enabled: !!root.runtime.ready && !root.runtime.busy && !root.testing
                                        onClicked: root.ui.runtimeCommand("delay_batch", [groupTile.modelData.name])
                                    }
                                }
                                Label {
                                    objectName: "proxy_group_subtitle"
                                    text: root.groupType(groupTile.modelData) + " · " + (root.query ? qsTr("%1 matches / %2 members").arg(groupTile.viewMembers.length).arg((groupTile.modelData.members || []).length) : qsTr("%1 members").arg((groupTile.modelData.members || []).length)) + (root.groupSourceText(groupTile.modelData) ? " · " + root.groupSourceText(groupTile.modelData) : "")
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
                                objectName: "proxy_group_progress_" + groupTile.modelData.name
                                text: groupTile.ownsProgress && root.progress.state ? (root.progress.scope === "node" ? qsTr("Single node") : qsTr("Group test")) + " · " + (root.progress.state === "Cancelled" ? qsTr("Cancelled") : root.progress.state === "Completed" ? qsTr("Completed") : qsTr("Testing…")) + " · " + qsTr("%1 / %2 tested · %3 unavailable").arg(root.progress.completed || 0).arg(root.progress.total || 0).arg(root.progress.failed || 0) : qsTr("No group test")
                                color: root.theme.muted
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            ActionButton {
                                objectName: "proxy_locate_group_" + groupTile.modelData.name
                                theme: root.theme
                                text: qsTr("Locate current")
                                implicitHeight: root.theme.compactHeight
                                enabled: root.canLocate(groupTile.modelData)
                                onClicked: root.locateCurrent(groupTile.modelData.name)
                            }
                            ActionButton {
                                objectName: "proxy_group_cancel_" + groupTile.modelData.name
                                theme: root.theme
                                text: qsTr("Cancel")
                                visible: root.testing && groupTile.ownsProgress
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
                            readonly property var members: groupTile.viewMembers
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
                                    // A recycled control must never retain focus for another member.
                                    onModelDataChanged: if (activeFocus || (testNode && testNode.activeFocus))
                                        searchField.forceActiveFocus(Qt.OtherFocusReason)
                                    readonly property var proxyState: {
                                        const revision = root.stateRevision;
                                        // The outer delegate may already be gone during pool teardown.
                                        return groupTile ? root.ui.proxyState(modelData, groupTile.modelData.name) : {text: "", result: "", description: ""};
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
                                        hovered: nodeTile.enabled && nodeTile.hovered && !testNode.hovered
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
                                                    color: !testNode.enabled ? "transparent" : testNode.down ? root.theme.selectedHover : testNode.hovered ? (nodeTile.current ? root.selectedDelayHover : root.theme.accentSurface) : "transparent"
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
