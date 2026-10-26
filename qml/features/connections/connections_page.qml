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
    function syncDemand(): void {
        ui.setDemand(demandKey, "connections", active);
    }
    onActiveChanged: syncDemand()
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
            text: root.ui.runtime.ready ? (root.ui.telemetry.connectionsStale ? qsTr("Connection data is stale") : qsTr("Live connections · refresh every second")) : qsTr("Start the core to view connections")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        ActionButton {
            theme: root.theme
            text: qsTr("Close all connections…")
            enabled: !!root.ui.runtime.ready
            onClicked: {
                root.closeEpoch = root.ui.runtime.epoch;
                confirmation.open();
            }
        }
        ComboBox {
            id: sorting
            model: [qsTr("Newest first"), qsTr("Download speed"), qsTr("Upload speed")]
            currentIndex: root.ui.liveConnections.sortMode
            onActivated: root.ui.liveConnections.sortMode = currentIndex
        }
        ActionButton {
            theme: root.theme
            text: qsTr("Close filtered connections…")
            objectName: "close_filtered_connections"
            enabled: !!root.ui.runtime.ready && root.ui.liveConnections.count > 0 && root.ui.liveConnections.count <= 500
            onClicked: {
                root.closeEpoch = root.ui.runtime.epoch;
                root.closeIds = root.ui.liveConnections.captureIds();
                filteredConfirmation.open();
            }
        }
        RuntimeList {
            id: liveList
            Layout.fillWidth: true
            Layout.fillHeight: true
            theme: root.theme
            heading: qsTr("Connections")
            listModel: root.ui.liveConnections
            actionText: qsTr("Close connection")
            describe: function (entry) {
                const m = entry.metadata || {};
                return (m.host || m.destinationIP || "—") + ":" + (m.destinationPort || "") + " · " + (m.network || "") + " · " + (m.process || "—") + "\n" + (entry.chains || []).join(" → ") + " · " + (entry.rule || "") + " " + (entry.rulePayload || "") + "\n↑ " + (entry.upload || 0) + " B · ↓ " + (entry.download || 0) + " B · " + (entry.start || "") + "\n↑ " + (entry.uploadRate === undefined ? "—" : entry.uploadRate.toFixed(0)) + " B/s · ↓ " + (entry.downloadRate === undefined ? "—" : entry.downloadRate.toFixed(0)) + " B/s";
            }
            onInvoked: function (entry) {
                root.ui.runtimeCommand("close_connection", [entry.epoch, entry.id]);
            }
        }
    }
    Dialog {
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
    Dialog {
        id: confirmation
        title: qsTr("Close every active connection?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.ui.runtimeCommand("close_all_connections", [root.closeEpoch, "confirmed"])
    }
}
