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
    property string selectedEpoch: ""
    property string selectedId: ""
    property var selectedEntry: ({})
    property bool selectedEnded: false
    property string selectedSample: ""
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
        updateSelection();
        details.open();
    }
    function durationText(entry): string {
        const start = Date.parse(entry.start || "");
        const sampled = Date.parse(selectedSample);
        return isNaN(start) || isNaN(sampled) ? qsTr("Unavailable") : qsTr("%1 s").arg(Math.max(0, Math.floor((sampled - start) / 1000)));
    }
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
        Label {
            objectName: "connection_status"
            text: !root.ui.runtime.ready ? qsTr("Start the core to view connections")
                : root.ui.telemetry.connectionsPaused ? qsTr("Connection data is too large or invalid; updates paused")
                : root.ui.telemetry.connectionsStale ? qsTr("Connection data is stale")
                : !root.ui.telemetry.connectionSample ? qsTr("Loading connections…") : qsTr("Live connections · refresh every second")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            visible: !!root.ui.telemetry.connectionSample
            text: qsTr("Sampled: %1").arg(root.ui.telemetry.connectionSample || "")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        ActionButton {
            theme: root.theme
            visible: !!root.ui.telemetry.connectionsPaused
            text: qsTr("Retry connection updates")
            onClicked: root.ui.runtimeCommand("resume_connections", [])
        }
        Label {
            visible: root.ui.liveConnections.query.length > 0
            text: qsTr("Filters affect this view; complete snapshots are still received.")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            visible: root.closeState.closeRemaining !== undefined && !root.closeState.closeAllResult
            text: qsTr("Close results · closed %1 · ended %2 · failed %3 · unknown %4 · remaining %5")
                .arg(root.closeState.closeSucceeded || 0).arg(root.closeState.closeEnded || 0)
                .arg(root.closeState.closeFailed || 0).arg(root.closeState.closeUnknown || 0)
                .arg(root.closeState.closeRemaining || 0)
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            visible: !!root.closeState.closeAllResult
            text: root.closeState.closeAllResult === "Running" ? qsTr("Closing all connections…")
                : root.closeState.closeAllResult === "Completed" ? qsTr("Close all completed")
                : root.closeState.closeAllResult === "Failed" ? qsTr("Close all failed") : qsTr("Close all result is unknown; check the latest snapshot")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        // Keep the close actions together, wrapping when the window is narrow.
        Flow {
            Layout.fillWidth: true
            spacing: 8
            ActionButton {
                theme: root.theme
                text: qsTr("Close all connections…")
                enabled: !!root.ui.runtime.ready && !root.closeState.closeRunning
                onClicked: {
                    root.closeEpoch = root.ui.runtime.epoch;
                    confirmation.open();
                }
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Close filtered connections…")
                objectName: "close_filtered_connections"
                enabled: !!root.ui.runtime.ready && !root.closeState.closeRunning && root.ui.liveConnections.count > 0 && root.ui.liveConnections.count <= 500
                onClicked: {
                    root.closeEpoch = root.ui.runtime.epoch;
                    root.closeIds = root.ui.liveConnections.captureIds();
                    filteredConfirmation.open();
                }
            }
        }
        // Heading and the sorting selector share one row; the combo box uses
        // the same hover highlight style as the buttons.
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label {
                text: qsTr("Connections") + " · " + root.ui.liveConnections.count
                color: root.theme.text
                font.pixelSize: root.theme.bodySize
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            ThemedComboBox {
                theme: root.theme
                id: sorting
                Accessible.name: qsTr("Sort connections")
                objectName: "connections_sorting"
                font.family: root.theme.fontFamily
                implicitHeight: root.theme.controlHeight
                // Padding lives on the control; the content Text has none.
                leftPadding: 12
                rightPadding: 30
                model: [qsTr("Newest first"), qsTr("Download speed"), qsTr("Upload speed")]
                currentIndex: root.ui.liveConnections.sortMode
                onActivated: root.ui.liveConnections.sortMode = currentIndex
            }
        }
        RuntimeList {
            id: liveList
            Layout.fillWidth: true
            Layout.fillHeight: true
            theme: root.theme
            // The heading row lives in the page so the sorting selector can
            // share it; an empty heading hides the built-in label.
            heading: ""
            listModel: root.ui.liveConnections
            actionText: qsTr("Close connection")
            secondaryActionText: qsTr("Details")
            describe: function (entry) {
                const m = entry.metadata || {};
                return (m.host || m.destinationIP || "—") + ":" + (m.destinationPort || "") + " · " + (m.network || "") + " · " + (m.process || "—") + "\n" + (entry.chains || []).join(" → ") + " · " + (entry.rule || "") + " " + (entry.rulePayload || "") + "\n↑ " + (entry.upload || 0) + " B · ↓ " + (entry.download || 0) + " B · " + (entry.start || "") + "\n↑ " + (entry.uploadRate === undefined ? "—" : entry.uploadRate.toFixed(0)) + " B/s · ↓ " + (entry.downloadRate === undefined ? "—" : entry.downloadRate.toFixed(0)) + " B/s";
            }
            onInvoked: function (entry) {
                root.ui.runtimeCommand("close_connection", [entry.epoch, entry.id]);
            }
            onSecondaryInvoked: function (entry) { root.showDetails(entry); }
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
        onClosed: { root.selectedId = ""; root.selectedEpoch = ""; root.selectedEntry = ({}); }
        ColumnLayout {
            anchors.fill: parent
            Label {
                text: qsTr("Duration: %1").arg(root.durationText(root.selectedEntry))
                color: root.theme.text
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    text: JSON.stringify(root.selectedEntry, null, 2)
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    color: root.theme.text
                    background: Item {}
                }
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Close connection")
                enabled: !root.selectedEnded && !!root.ui.runtime.ready && !root.closeState.closeRunning
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
