pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    objectName: "logs_page"
    required property UiState ui
    required property ThemeTokens theme
    readonly property string demandKey: ui.createDemandKey("logs")
    property bool paused: false
    property bool active: visible && ui.uiVisible && !ui.replay
    // Read once per change notification so the per-second traffic sample does
    // not re-materialize the whole telemetry/runtime maps in every binding.
    readonly property var telemetry: ui.telemetry
    readonly property var runtime: ui.runtime
    function syncDemand(): void {
        ui.setDemand(demandKey + ":view", "logs", active);
        ui.setDemand(demandKey + ":live", "log_feed", active && !paused);
    }
    onActiveChanged: syncDemand()
    onPausedChanged: {
        ui.setLogsPaused(paused);
        syncDemand();
    }
    Component.onCompleted: {
        ui.setLogsPaused(paused);
        syncDemand();
    }
    Component.onDestruction: {
        ui.setDemand(demandKey + ":live", "log_feed", false);
        ui.setDemand(demandKey + ":view", "logs", false);
        ui.setLogsPaused(false);
    }
    ColumnLayout {
        anchors.fill: parent
        Flow {
            Layout.fillWidth: true
            spacing: 8
            ActionButton {
                theme: root.theme
                text: root.paused ? qsTr("Resume receiving") : qsTr("Pause receiving")
                objectName: "pause_logs"
                onClicked: root.paused = !root.paused
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Clear buffer")
                onClicked: root.ui.runtimeCommand("clear_logs")
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Export redacted logs")
                onClicked: root.ui.runtimeCommand("export_logs")
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Open diagnostics")
                visible: !!(root.runtime.features || {}).exportUrl
                onClicked: Qt.openUrlExternally(root.runtime.features.exportUrl)
            }
            ComboBox {
                id: source
                model: [qsTr("All sources"), "Core", "App"]
                onCurrentIndexChanged: root.ui.liveLogs.sourceFilter = currentIndex === 0 ? "" : currentText
            }
            ComboBox {
                id: level
                model: [qsTr("All levels"), "info", "warning", "error", "debug"]
                onCurrentIndexChanged: root.ui.liveLogs.levelFilter = currentIndex === 0 ? "" : currentText
            }
            CheckBox {
                id: follow
                text: qsTr("Follow latest")
                checked: true
            }
        }
        Label {
            text: qsTr("Dropped entries: %1 · Pausing creates a gap in core logs").arg(root.telemetry.droppedLogs || 0)
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        RuntimeList {
            theme: root.theme
            Layout.fillWidth: true
            Layout.fillHeight: true
            heading: qsTr("Logs")
            followTail: follow.checked && !root.paused
            listModel: root.ui.liveLogs
            describe: function (entry) {
                return entry.time + " · " + entry.source + " · " + entry.level + "\n" + entry.message;
            }
        }
    }
}
