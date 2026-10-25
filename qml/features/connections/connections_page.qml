pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    property bool active: visible && ui.uiVisible && !ui.replay
    property string closeEpoch: ""
    property var closeIds: []
    function syncDemand(): void { ui.setDemand("connections:live", "connections", active); }
    onActiveChanged: syncDemand()
    Component.onCompleted: syncDemand()
    Component.onDestruction: ui.setDemand("connections:live", "connections", false)
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
            text: root.ui.runtime.ready ? (root.ui.runtime.features && root.ui.runtime.features.connectionsStale ? qsTr("Connection data is stale") : qsTr("Live connections · refresh every second")) : qsTr("Start the core to view connections")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        ActionButton {
            theme: root.theme
            text: qsTr("Close all connections…")
            enabled: !!root.ui.runtime.ready
            onClicked: { root.closeEpoch = root.ui.runtime.epoch; confirmation.open(); }
        }
        ComboBox { id: sorting; model: [qsTr("Newest first"), qsTr("Download speed"), qsTr("Upload speed")] }
        ActionButton {
            theme: root.theme
            text: qsTr("Close filtered connections…")
            enabled: !!root.ui.runtime.ready && liveList.filtered.length > 0 && liveList.filtered.length <= 500
            onClicked: {
                root.closeEpoch = root.ui.runtime.epoch;
                root.closeIds = liveList.filtered.map(entry => entry.id);
                filteredConfirmation.open();
            }
        }
        RuntimeList {
            id: liveList
            Layout.fillWidth: true
            Layout.fillHeight: true
            theme: root.theme
            heading: qsTr("Connections")
            entries: root.ui.runtime.connections || []
            compare: function(a, b) {
                if (sorting.currentIndex === 1) return (b.downloadRate || 0) - (a.downloadRate || 0);
                if (sorting.currentIndex === 2) return (b.uploadRate || 0) - (a.uploadRate || 0);
                return (b.start || "").localeCompare(a.start || "");
            }
            actionText: qsTr("Close connection")
            describe: function(entry) {
                const m = entry.metadata || {};
                return (m.host || m.destinationIP || "—") + ":" + (m.destinationPort || "") + " · " + (m.network || "") + " · " + (m.process || "—") + "\n" + (entry.chains || []).join(" → ") + " · " + (entry.rule || "") + " " + (entry.rulePayload || "") + "\n↑ " + (entry.upload || 0) + " B · ↓ " + (entry.download || 0) + " B · " + (entry.start || "") + "\n↑ " + (entry.uploadRate === undefined ? "—" : entry.uploadRate.toFixed(0)) + " B/s · ↓ " + (entry.downloadRate === undefined ? "—" : entry.downloadRate.toFixed(0)) + " B/s";
            }
            onInvoked: function(entry) { root.ui.runtimeCommand("close_connection", [entry.epoch, entry.id]); }
        }
    }
    Dialog {
        id: filteredConfirmation
        title: qsTr("Close %1 captured connections?").arg(root.closeIds.length)
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.ui.runtimeCommand("close_filtered_connections", [root.closeEpoch, "confirmed"].concat(root.closeIds))
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
