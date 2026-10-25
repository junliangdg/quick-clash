pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    property string generation: ""
    function refresh(): void { if (ui.runtime.ready) ui.runtimeCommand("rules"); }
    Component.onCompleted: refresh()
    Connections {
        target: root.ui
        function onRuntimeChanged(): void {
            const generation = (root.ui.runtime.epoch || "") + ":" + (root.ui.runtime.selectionGeneration || "");
            if (root.ui.runtime.ready && root.generation !== generation) {
                root.generation = generation;
                root.refresh();
            }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        ComboBox {
            id: section
            model: [qsTr("Rules"), qsTr("Providers")]
            onActivated: root.ui.runtimeCommand(currentIndex === 0 ? "rules" : "providers")
        }
        ActionButton {
            theme: root.theme
            text: qsTr("Refresh")
            enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy
            onClicked: root.ui.runtimeCommand(section.currentIndex === 0 ? "rules" : "providers")
        }
        Label {
            text: root.ui.runtime.ready ? qsTr("Filtering keeps the effective rule order unchanged") : qsTr("Start the core to view rules")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        RuntimeList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            theme: root.theme
            heading: section.currentText
            entries: section.currentIndex === 0 ? (root.ui.runtime.rules || []) : (root.ui.runtime.providers || [])
            actionText: section.currentIndex === 1 ? qsTr("Update provider") : ""
            describe: function(entry) {
                return section.currentIndex === 0 ? (entry.type || "") + " · " + (entry.payload || "") + " → " + (entry.proxy || "") : entry.name + " · " + entry.kind + " · " + (entry.count || 0) + " · " + (entry.updatedAt || qsTr("Unknown"));
            }
            onInvoked: function(entry) { root.ui.runtimeCommand("update_provider", [entry.kind, entry.name]); }
        }
    }
}
