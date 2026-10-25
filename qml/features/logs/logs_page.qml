pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui
Item {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    property bool paused: false
    property var frozen: []
    property bool active: visible && ui.uiVisible && !paused
    function syncDemand(): void { ui.setDemand("logs:live", "logs", active); }
    onActiveChanged: syncDemand()
    Component.onCompleted: syncDemand()
    Component.onDestruction: ui.setDemand("logs:live", "logs", false)
    ColumnLayout {
        anchors.fill: parent
        Flow {
            Layout.fillWidth: true
            spacing: 8
            ActionButton {
                theme: root.theme
                text: root.paused ? qsTr("Resume receiving") : qsTr("Pause receiving")
                onClicked: {
                    if (!root.paused) root.frozen = (root.ui.runtime.logs || []).slice();
                    root.paused = !root.paused;
                }
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Clear buffer")
                onClicked: { root.frozen = []; root.ui.runtimeCommand("clear_logs"); }
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Export redacted logs")
                onClicked: root.ui.runtimeCommand("export_logs")
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Open diagnostics")
                visible: !!(root.ui.runtime.features || {}).exportUrl
                onClicked: Qt.openUrlExternally(root.ui.runtime.features.exportUrl)
            }
            ComboBox {
                id: source
                model: [qsTr("All sources"), "Core", "App"]
            }
            ComboBox {
                id: level
                model: [qsTr("All levels"), "info", "warning", "error", "debug"]
            }
            CheckBox { id: follow; text: qsTr("Follow latest"); checked: true }
        }
        Label {
            text: qsTr("Dropped entries: %1 · Pausing creates a gap in core logs").arg(root.ui.runtime.droppedLogs || 0)
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
            entries: root.paused ? root.frozen : (root.ui.runtime.logs || [])
            accept: function(entry) { return (source.currentIndex === 0 || entry.source === source.currentText) && (level.currentIndex === 0 || entry.level === level.currentText); }
            describe: function(entry) { return entry.time + " · " + entry.source + " · " + entry.level + "\n" + entry.message; }
        }
    }
}
