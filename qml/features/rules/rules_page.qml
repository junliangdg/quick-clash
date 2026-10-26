pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    objectName: "rules_page"
    required property UiState ui
    required property ThemeTokens theme
    readonly property string demandKey: ui.createDemandKey("rules")
    property bool active: visible && ui.uiVisible && !ui.replay
    function syncDemand(): void {
        ui.setDemand(demandKey, section.currentIndex === 0 ? "rules" : "providers", active);
    }
    onActiveChanged: syncDemand()
    Component.onCompleted: syncDemand()
    Component.onDestruction: ui.setDemand(demandKey, "rules", false)
    ColumnLayout {
        anchors.fill: parent
        ComboBox {
            id: section
            objectName: "rules_section"
            model: [qsTr("Rules"), qsTr("Providers")]
            onCurrentIndexChanged: root.syncDemand()
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
            listModel: section.currentIndex === 0 ? root.ui.liveRules : root.ui.liveProviders
            actionText: section.currentIndex === 1 ? qsTr("Update provider") : ""
            describe: function (entry) {
                return section.currentIndex === 0 ? (entry.type || "") + " · " + (entry.payload || "") + " → " + (entry.proxy || "") : entry.name + " · " + entry.kind + " · " + (entry.count || 0) + " · " + (entry.updatedAt || qsTr("Unknown"));
            }
            onInvoked: function (entry) {
                root.ui.runtimeCommand("update_provider", [entry.kind, entry.name]);
            }
        }
    }
}
