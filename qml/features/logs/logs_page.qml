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
    readonly property var exportPreview: (runtime.features || {}).logExportPreview || ({})
    property bool exporting: false
    readonly property bool exportReady: !!exportPreview.token && exportPreview.messagesHidden === hideLogMessages.checked
    Binding {
        target: logExport.standardButton(Dialog.Save)
        property: "enabled"
        value: root.exportReady
        when: logExport.visible
    }
    onExportPreviewChanged: {
        if (exporting && exportPreview.token && !logExport.visible)
            logExport.open();
    }
    ThemedDialog {
        id: logExport
        objectName: "log_export_dialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(640, parent ? parent.width - 24 : 640)
        height: Math.min(560, parent ? parent.height - 24 : 560)
        theme: root.theme
        title: qsTr("Review log export")
        standardButtons: Dialog.Save | Dialog.Cancel
        onOpened: hideLogMessages.checked = true
        onAccepted: {
            root.exporting = false;
            root.ui.runtimeCommand("export_logs", [root.exportPreview.token || ""]);
        }
        onRejected: {
            root.exporting = false;
            root.ui.runtimeCommand("cancel_log_export");
        }
        contentItem: ScrollView {
            id: logExportScroll
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: logExportScroll.availableWidth
                Label {
                    text: qsTr("Exports a frozen copy of the whole buffer. Source and level filters only affect the page. Preview text is limited to 16 KiB; the file contains the complete bounded snapshot.")
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: root.theme.muted
                }
                ThemedCheckBox {
                    id: hideLogMessages
                    objectName: "log_export_hide_messages"
                    theme: root.theme
                    text: qsTr("Hide messages, domains and process names")
                    checked: true
                    enabled: root.exportReady
                    onToggled: root.ui.runtimeCommand("preview_logs", [checked ? "hide_messages" : "include_messages", root.exportPreview.token || ""])
                }
                ThemedTextArea {
                    theme: root.theme
                    objectName: "log_export_preview"
                    Accessible.name: qsTr("Log export preview")
                    Layout.fillWidth: true
                    Layout.preferredHeight: 260
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    text: root.exportPreview.text || ""
                }
            }
        }
    }
    function syncDemand(): void {
        ui.setDemand(demandKey + ":view", "logs", active);
        ui.setDemand(demandKey + ":live", "log_feed", active && !paused);
    }
    onActiveChanged: {
        syncDemand();
        if (!active) {
            exporting = false;
            logExport.close();
        }
    }
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
                text: qsTr("Export redacted logs…")
                objectName: "preview_log_export"
                onClicked: {
                    root.exporting = true;
                    root.ui.runtimeCommand("preview_logs", ["hide_messages"]);
                }
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Open diagnostics")
                visible: !!(root.runtime.features || {}).exportUrl
                onClicked: Qt.openUrlExternally(root.runtime.features.exportUrl)
            }
            ThemedComboBox {
                id: source
                Accessible.name: qsTr("Log source")
                theme: root.theme
                objectName: "log_source_filter"
                model: [qsTr("All sources"), "Core", "App", "Service"]
                onCurrentIndexChanged: root.ui.liveLogs.sourceFilter = currentIndex === 0 ? "" : currentText
            }
            ThemedComboBox {
                id: level
                Accessible.name: qsTr("Log level")
                theme: root.theme
                model: [qsTr("All levels"), "info", "warning", "error", "debug", "silent", "unknown"]
                onCurrentIndexChanged: root.ui.liveLogs.levelFilter = currentIndex === 0 ? "" : currentText
            }
            ThemedCheckBox {
                id: follow
                theme: root.theme
                text: qsTr("Follow latest")
                checked: true
            }
        }
        Label {
            text: qsTr("Dropped: %1 · Truncated: %2 · Gaps: %3 · Buffer: %4 bytes").arg(root.telemetry.droppedLogs || 0).arg(root.telemetry.truncatedLogs || 0).arg(root.telemetry.logGaps || 0).arg(root.telemetry.logBytes || 0)
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
                return entry.time + " · " + entry.source + " · " + entry.level + "\n" + entry.message + (entry.truncated ? "\n" + qsTr("[Message truncated]") : "");
            }
        }
    }
}
