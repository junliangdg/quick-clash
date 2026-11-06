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
    readonly property var columns: [
        { "title": qsTr("Time"), "weight": 0.9 },
        { "title": qsTr("Level"), "weight": 0.6 },
        { "title": qsTr("Source"), "weight": 0.7 },
        { "title": qsTr("Message"), "weight": 4.7 }
    ]
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
        contentItem: ThemedScrollView {
            id: logExportScroll
            theme: root.theme
            form: logExportForm
            ColumnLayout {
                id: logExportForm
                Label {
                    text: qsTr("Exports a frozen copy of the whole buffer. Source and level filters only affect the page. Preview text is limited to 16 KiB; the file contains the complete bounded snapshot.")
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: root.theme.muted
                }
                ThemedCheckBox {
                    id: hideLogMessages
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
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
    function levelColor(entry): color {
        if (entry.level === "error")
            return root.theme.error;
        if (entry.level === "warning")
            return root.theme.warning;
        if (entry.level === "debug" || entry.level === "unknown" || entry.level === "silent")
            return root.theme.muted;
        return root.theme.text;
    }
    function cellText(entry, index): string {
        switch (index) {
        case 0:
            return format.time(entry.time);
        case 1:
            return entry.level || "—";
        case 2:
            return entry.source || "—";
        case 3:
            return (entry.message || "") + (entry.truncated ? " …" : "");
        }
        return "";
    }
    function cellColor(entry, index): color {
        return index === 1 ? root.levelColor(entry) : index === 3 ? root.theme.text : root.levelColor(entry);
    }
    function describe(entry): string {
        return format.time(entry.time) + " · " + (entry.level || "—") + " · " + (entry.source || "—") + "\n" + (entry.message || "") + (entry.truncated ? "\n" + qsTr("[Message truncated]") : "");
    }
    RuntimeFormat { id: format }
    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                text: qsTr("Logs") + " · " + root.ui.liveLogs.count
                color: root.theme.text
                font.pixelSize: root.theme.bodySize
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Label {
                text: root.paused ? qsTr("Reception paused") : root.runtime.ready ? qsTr("Receiving") : qsTr("Start the core to view logs")
                color: root.paused ? root.theme.warning : root.runtime.ready ? root.theme.muted : root.theme.warning
                font.pixelSize: root.theme.supportingSize
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: width >= root.theme.scaledSize(420) ? 2 : 1
            columnSpacing: 8
            rowSpacing: 8
            RuntimeFilter {
                theme: root.theme
                listModel: root.ui.liveLogs
                placeholderText: qsTr("Search messages, sources or levels")
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            ActionButton {
                theme: root.theme
                text: root.paused ? qsTr("Resume receiving") : qsTr("Pause receiving")
                objectName: "pause_logs"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                onClicked: root.paused = !root.paused
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            ThemedComboBox {
                width: Math.min(implicitWidth, parent.width)
                id: source
                Accessible.name: qsTr("Log source")
                theme: root.theme
                objectName: "log_source_filter"
                model: [qsTr("All sources"), "Core", "App", "Service"]
                onCurrentIndexChanged: root.ui.liveLogs.sourceFilter = currentIndex === 0 ? "" : currentText
            }
            ThemedComboBox {
                width: Math.min(implicitWidth, parent.width)
                id: level
                Accessible.name: qsTr("Log level")
                theme: root.theme
                model: [qsTr("All levels"), "info", "warning", "error", "debug", "silent", "unknown"]
                onCurrentIndexChanged: root.ui.liveLogs.levelFilter = currentIndex === 0 ? "" : currentText
            }
            ThemedCheckBox {
                width: Math.min(implicitWidth, parent.width)
                id: follow
                theme: root.theme
                text: qsTr("Follow latest")
                checked: true
            }
            ActionButton {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTr("Clear buffer")
                onClicked: root.ui.runtimeCommand("clear_logs")
            }
            ActionButton {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTr("Export redacted logs…")
                objectName: "preview_log_export"
                onClicked: {
                    root.exporting = true;
                    root.ui.runtimeCommand("preview_logs", ["hide_messages"]);
                }
            }
            ActionButton {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTr("Open diagnostics")
                visible: !!(root.runtime.features || {}).exportUrl
                onClicked: Qt.openUrlExternally(root.runtime.features.exportUrl)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: logList.horizontalOverflow || ((!!root.ui.liveLogs.query || !!root.ui.liveLogs.sourceFilter || !!root.ui.liveLogs.levelFilter) && root.ui.liveLogs.count === 0) || (follow.checked && !root.paused && !logList.userFollowing)
            Label {
                Layout.fillWidth: true
                text: [follow.checked && !root.paused && !logList.userFollowing ? qsTr("Browsing history; new logs are still received") : "", logList.horizontalOverflow ? qsTr("Scroll horizontally to see all columns") : ""].filter(value => !!value).join(" · ")
                color: root.theme.muted
                font.pixelSize: root.theme.supportingSize
                wrapMode: Text.WordWrap
            }
            ActionButton {
                theme: root.theme
                Layout.maximumWidth: parent.width / 2
                visible: (!!root.ui.liveLogs.query || !!root.ui.liveLogs.sourceFilter || !!root.ui.liveLogs.levelFilter) && root.ui.liveLogs.count === 0
                objectName: "logs_clear_filter"
                text: qsTr("Clear filter")
                onClicked: { root.ui.liveLogs.query = ""; source.currentIndex = 0; level.currentIndex = 0; }
            }
            ActionButton {
                theme: root.theme
                Layout.maximumWidth: parent.width / 2
                objectName: "logs_jump_latest"
                visible: follow.checked && !root.paused && !logList.userFollowing
                text: qsTr("Jump to latest")
                onClicked: logList.jumpToLatest()
            }
        }
        RuntimeList {
            id: logList
            Layout.fillWidth: true
            Layout.fillHeight: true
            theme: root.theme
            listModel: root.ui.liveLogs
            columns: root.columns
            rowHeight: root.theme.rowHeight
            minimumWidth: root.theme.scaledSize(620)
            cellText: root.cellText
            cellColor: root.cellColor
            describe: root.describe
            expandable: true
            actionText: ""
            followTail: follow.checked && !root.paused
            emptyText: root.ui.liveLogs.query || root.ui.liveLogs.sourceFilter || root.ui.liveLogs.levelFilter ? qsTr("No matching logs") : root.paused ? qsTr("Reception paused") : !root.runtime.ready ? qsTr("Start the core to view logs") : qsTr("No logs yet")
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Dropped: %1 · Truncated: %2 · Gaps: %3 · Buffer: %4 bytes").arg(root.telemetry.droppedLogs || 0).arg(root.telemetry.truncatedLogs || 0).arg(root.telemetry.logGaps || 0).arg(root.telemetry.logBytes || 0)
            color: root.theme.muted
            font.pixelSize: root.theme.supportingSize
            elide: Text.ElideRight
        }
    }
}
