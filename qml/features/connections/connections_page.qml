pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    objectName: "connections_page"
    required property UiState ui
    required property ThemeTokens theme
    readonly property string demandKey: ui.createDemandKey("connections")
    property bool active: visible && ui.uiVisible && !ui.replay
    property string closeEpoch: ""
    property var closeIds: []
    readonly property var closeState: ui.runtime.features || ({})
    readonly property var telemetry: ui.telemetry
    readonly property var runtime: ui.runtime
    property string selectedEpoch: ""
    property string selectedId: ""
    property var selectedEntry: ({})
    property bool selectedEnded: false
    property string selectedSample: ""
    property bool rawVisible: false
    readonly property string selectedKey: selectedId ? selectedEpoch + String.fromCharCode(0) + selectedId : ""
    readonly property var columns: [
        { "title": qsTr("Target"), "weight": 2.4 },
        { "title": qsTr("Chain"), "weight": 2.0 },
        { "title": qsTr("Download"), "weight": 0.9, "numeric": true },
        { "title": qsTr("Upload"), "weight": 0.9, "numeric": true }
    ]
    function statusText(): string {
        return !runtime.ready ? qsTr("Start the core to view connections")
            : telemetry.connectionsPaused ? qsTr("Connection data is too large or invalid; updates paused")
            : telemetry.connectionsStale ? qsTr("Connection data is stale")
            : !telemetry.connectionSample ? qsTr("Loading connections…") : qsTr("Live connections · refresh every second");
    }
    function statusColor(): color {
        return telemetry.connectionsPaused || telemetry.connectionsStale || !runtime.ready ? theme.warning : theme.muted;
    }
    function updateSelection(): void {
        if (!selectedId)
            return;
        const entry = ui.liveConnections.connection(selectedEpoch, selectedId);
        selectedEnded = !entry.id || selectedEpoch !== ui.runtime.epoch;
        if (!selectedEnded) {
            selectedEntry = entry;
            selectedSample = ui.telemetry.connectionSample || "";
        }
    }
    function showDetails(entry): void {
        selectedEpoch = entry.epoch;
        selectedId = entry.id;
        selectedEntry = entry;
        rawVisible = false;
        updateSelection();
        details.open();
    }
    function durationText(entry): string {
        const start = Date.parse(entry.start || "");
        const sampled = Date.parse(selectedSample);
        return isNaN(start) || isNaN(sampled) ? qsTr("Unavailable") : qsTr("%1 s").arg(Math.max(0, Math.floor((sampled - start) / 1000)));
    }
    function rateText(value): string {
        return typeof value === "number" && isFinite(value) ? format.bytes(value, true) : "—";
    }
    function processText(entry): string {
        const m = entry.metadata || {};
        return (m.process || qsTr("Process unavailable")) + (m.network ? " · " + m.network.toUpperCase() : "");
    }
    function totalText(entry): string {
        return qsTr("Down %1 · Up %2").arg(format.bytes(entry.download, false)).arg(format.bytes(entry.upload, false));
    }
    function ruleText(entry): string {
        return ((entry.rule || "") + (entry.rulePayload ? " · " + entry.rulePayload : "")) || "—";
    }
    function describe(entry): string {
        return format.target(entry) + " · " + processText(entry) + "\n" + format.chain(entry) + " · " + ruleText(entry) + "\n" + qsTr("Download") + " " + rateText(entry.downloadRate) + " · " + qsTr("Upload") + " " + rateText(entry.uploadRate) + "\n" + totalText(entry);
    }
    function cellText(entry, index): string {
        switch (index) {
        case 0:
            return format.target(entry);
        case 1:
            return format.chain(entry);
        case 2:
            return rateText(entry.downloadRate);
        case 3:
            return rateText(entry.uploadRate);
        }
        return "";
    }
    function cellColor(entry, index): color {
        if (index === 2)
            return entry.downloadRate === undefined ? theme.muted : theme.download;
        if (index === 3)
            return entry.uploadRate === undefined ? theme.muted : theme.upload;
        return theme.text;
    }
    RuntimeFormat { id: format }
    Connections {
        target: root.ui.liveConnections
        function onEntriesChanged(): void { root.updateSelection(); }
    }
    Connections {
        target: root.ui
        function onRuntimeChanged(): void { root.updateSelection(); }
    }
    function syncDemand(): void {
        ui.setDemand(demandKey, "connections", active);
    }
    onActiveChanged: {
        syncDemand();
        if (!active) {
            details.close();
            confirmation.close();
            filteredConfirmation.close();
            selectedId = "";
            selectedEpoch = "";
            selectedEntry = ({});
            closeIds = [];
        }
    }
    Component.onCompleted: syncDemand()
    Component.onDestruction: ui.setDemand(demandKey, "connections", false)
    ReplayList {
        anchors.fill: parent
        visible: root.ui.replay
        ui: root.ui
        theme: root.theme
        pageId: "connections"
        stream: "connections"
        replayModel: ui.connectionModel
    }
    ColumnLayout {
        anchors.fill: parent
        visible: !root.ui.replay
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                text: qsTr("Connections") + " · " + root.ui.liveConnections.count
                color: root.theme.text
                font.pixelSize: root.theme.bodySize
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Label {
                objectName: "connection_status"
                text: root.statusText()
                color: root.statusColor()
                font.pixelSize: root.theme.supportingSize
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                Layout.maximumWidth: root.width * 0.6
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: width >= root.theme.scaledSize(760) ? 4 : width >= root.theme.scaledSize(420) ? 2 : 1
            columnSpacing: 8
            rowSpacing: 8
            RuntimeFilter {
                theme: root.theme
                listModel: root.ui.liveConnections
                placeholderText: qsTr("Host, process, chain or rule")
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            ThemedComboBox {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                id: sorting
                theme: root.theme
                Accessible.name: qsTr("Sort connections")
                objectName: "connections_sorting"
                implicitHeight: root.theme.controlHeight
                leftPadding: 12
                rightPadding: 30
                model: [qsTr("Newest first"), qsTr("Download speed"), qsTr("Upload speed")]
                currentIndex: root.ui.liveConnections.sortMode
                onActivated: root.ui.liveConnections.sortMode = currentIndex
            }
            ActionButton {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                theme: root.theme
                text: qsTr("Close filtered…")
                objectName: "close_filtered_connections"
                enabled: !!root.runtime.ready && !root.closeState.closeRunning && root.ui.liveConnections.count > 0 && root.ui.liveConnections.count <= 500
                onClicked: {
                    root.closeEpoch = root.runtime.epoch;
                    root.closeIds = root.ui.liveConnections.captureIds();
                    filteredConfirmation.open();
                }
            }
            ActionButton {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                theme: root.theme
                text: qsTr("Close all…")
                enabled: !!root.runtime.ready && !root.closeState.closeRunning
                onClicked: {
                    root.closeEpoch = root.runtime.epoch;
                    confirmation.open();
                }
            }
            ActionButton {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                theme: root.theme
                visible: !!root.telemetry.connectionsPaused
                text: qsTr("Retry connection updates")
                onClicked: root.ui.runtimeCommand("resume_connections", [])
            }
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            color: root.theme.muted
            font.pixelSize: root.theme.supportingSize
            elide: Text.ElideRight
            text: {
                if (root.closeState.closeAllResult === "Running")
                    return qsTr("Closing all connections…");
                if (root.closeState.closeRemaining !== undefined)
                    return qsTr("Close results · closed %1 · ended %2 · failed %3 · unknown %4 · remaining %5")
                        .arg(root.closeState.closeSucceeded || 0).arg(root.closeState.closeEnded || 0)
                        .arg(root.closeState.closeFailed || 0).arg(root.closeState.closeUnknown || 0)
                        .arg(root.closeState.closeRemaining || 0);
                if (root.closeState.closeAllResult === "Completed")
                    return qsTr("Close all completed");
                if (root.closeState.closeAllResult === "Failed")
                    return qsTr("Close all failed");
                return "";
            }
        }
        RuntimeList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            theme: root.theme
            listModel: root.ui.liveConnections
            columns: root.columns
            rowHeight: root.theme.scaledSize(48)
            minimumWidth: root.theme.scaledSize(680)
            cellText: root.cellText
            supportingText: function (entry, index) { return index === 0 ? root.processText(entry) : ""; }
            cellColor: root.cellColor
            describe: root.describe
            actionText: ""
            selectedKey: root.selectedKey
            onInvoked: function (entry) { root.showDetails(entry); }
        }
    }
    ThemedDialog {
        id: details
        objectName: "connection_details"
        theme: root.theme
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(680, parent.width - 32)
        height: Math.min(600, parent.height - 32)
        title: root.selectedEnded ? qsTr("Connection ended") : qsTr("Connection details")
        modal: true
        standardButtons: Dialog.Close
        onClosed: {
            root.selectedId = "";
            root.selectedEpoch = "";
            root.selectedEntry = ({});
            root.rawVisible = false;
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 16
                rowSpacing: 6
                Label { text: qsTr("Target"); color: root.theme.muted; font.pixelSize: root.theme.supportingSize }
                Label {
                    text: format.target(root.selectedEntry)
                    color: root.theme.text
                    font.pixelSize: root.theme.bodySize
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Label { text: qsTr("Process"); color: root.theme.muted; font.pixelSize: root.theme.supportingSize }
                Label {
                    text: root.processText(root.selectedEntry)
                    color: root.theme.text
                    font.pixelSize: root.theme.bodySize
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Label { text: qsTr("Chain"); color: root.theme.muted; font.pixelSize: root.theme.supportingSize }
                Label {
                    text: format.chain(root.selectedEntry)
                    color: root.theme.text
                    font.pixelSize: root.theme.bodySize
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                }
                Label { text: qsTr("Rule"); color: root.theme.muted; font.pixelSize: root.theme.supportingSize }
                Label {
                    text: root.ruleText(root.selectedEntry)
                    color: root.theme.text
                    font.pixelSize: root.theme.bodySize
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Label { text: qsTr("Rate"); color: root.theme.muted; font.pixelSize: root.theme.supportingSize }
                Label {
                    text: qsTr("Download %1 · Upload %2").arg(root.rateText(root.selectedEntry.downloadRate)).arg(root.rateText(root.selectedEntry.uploadRate))
                    color: root.theme.text
                    font.pixelSize: root.theme.bodySize
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Label { text: qsTr("Transferred"); color: root.theme.muted; font.pixelSize: root.theme.supportingSize }
                Label {
                    text: root.totalText(root.selectedEntry)
                    color: root.theme.text
                    font.pixelSize: root.theme.bodySize
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Label { text: qsTr("Duration"); color: root.theme.muted; font.pixelSize: root.theme.supportingSize }
                Label {
                    text: root.durationText(root.selectedEntry)
                    color: root.theme.text
                    font.pixelSize: root.theme.bodySize
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
            ThemedCheckBox {
                theme: root.theme
                text: qsTr("Raw JSON")
                checked: root.rawVisible
                onToggled: root.rawVisible = checked
            }
            ThemedScrollView {
                theme: root.theme
                form: rawJson
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.rawVisible
                ThemedTextArea {
                    id: rawJson
                    theme: root.theme
                    Accessible.name: details.title
                    text: JSON.stringify(root.selectedEntry, null, 2)
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    color: root.theme.text
                    font.pixelSize: root.theme.supportingSize
                }
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Close connection")
                enabled: !root.selectedEnded && !!root.runtime.ready && !root.closeState.closeRunning
                onClicked: root.ui.runtimeCommand("close_connection", [root.selectedEpoch, root.selectedId])
            }
        }
    }
    ThemedDialog {
        theme: root.theme
        id: filteredConfirmation
        objectName: "filtered_connections_confirmation"
        title: qsTr("Close %1 captured connections?").arg(root.closeIds.length)
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            const args = [root.closeEpoch, "confirmed"];
            for (let i = 0; i < root.closeIds.length; ++i)
                args.push(root.closeIds[i]);
            root.ui.runtimeCommand("close_filtered_connections", args);
        }
    }
    ThemedDialog {
        theme: root.theme
        id: confirmation
        title: qsTr("Close every active connection?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.ui.runtimeCommand("close_all_connections", [root.closeEpoch, "confirmed"])
    }
}
