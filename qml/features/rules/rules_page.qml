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
    readonly property var pageState: section.currentIndex === 0 ? (ui.telemetry.rulePage || {}) : (ui.telemetry.providerPage || {})
    readonly property bool canRefresh: !!ui.runtime.ready && !ui.runtime.busy && !pageState.loading
    function syncDemand(): void {
        ui.setDemand(demandKey, section.currentIndex === 0 ? "rules" : "providers", active);
    }
    function resultText(value): string {
        switch (value) {
        case "Updating":
            return qsTr("Updating");
        case "Updated":
            return qsTr("Update completed; content may be unchanged");
        case "PendingApply":
            return qsTr("Snapshot saved; pending apply");
        case "Uncertain":
            return qsTr("Update outcome uncertain; metadata reloaded");
        case "Failed":
            return qsTr("Update failed; previous data retained");
        default:
            return qsTr("No update in this view");
        }
    }
    function describeEntry(entry): string {
        const count = typeof entry.count === "number" ? String(entry.count) : qsTr("Unknown");
        const updated = entry.updatedAt || qsTr("Unknown");
        if (section.currentIndex === 0) {
            const source = entry.providerName ? qsTr("Rule provider: %1").arg(entry.providerName) : qsTr("Effective configuration");
            return "#" + (entry.position || "?") + " · " + (entry.type || "") + " · " + (entry.payload || "") + " → " + (entry.proxy || "") + "\n" + source + (entry.providerName ? " · " + qsTr("Items: %1 · Updated: %2").arg(count).arg(updated) + " · " + qsTr("Limited information") : "");
        }
        return entry.name + " · " + (entry.mode === "runtime" ? qsTr("Core runtime") : qsTr("Selected profile snapshot")) + " · " + entry.kind + (entry.type ? " · " + entry.type : "") + (entry.behavior ? " · " + entry.behavior : "") + "\n" + qsTr("Items: %1 · Updated: %2").arg(count).arg(updated) + "\n" + resultText(entry.lastResult) + (entry.lastError ? " · " + entry.lastError : "") + (entry.stale ? " · " + qsTr("Stale data") : "");
    }
    onActiveChanged: {
        syncDemand();
        if (!active && editor)
            editor.close();
    }
    Component.onCompleted: syncDemand()
    Component.onDestruction: ui.setDemand(demandKey, "rules", false)
    ProfileEditor {
        id: editor
        parent: Overlay.overlay
        ui: root.ui
        theme: root.theme
    }
    ColumnLayout {
        anchors.fill: parent
        Flow {
            Layout.fillWidth: true
            spacing: 8
            ThemedComboBox {
                id: section
                Accessible.name: qsTr("Rules or providers")
                theme: root.theme
                objectName: "rules_section"
                model: [qsTr("Rules"), qsTr("Providers")]
                onCurrentIndexChanged: root.syncDemand()
            }
            ActionButton {
                theme: root.theme
                objectName: "refresh_rules"
                text: root.pageState.loading ? qsTr("Loading…") : qsTr("Refresh")
                enabled: root.canRefresh
                onClicked: root.ui.runtimeCommand(section.currentIndex === 0 ? "rules" : "providers")
            }
            ActionButton {
                theme: root.theme
                objectName: "edit_rules"
                text: qsTr("Edit selected profile overrides…")
                enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly
                onClicked: editor.edit(root.ui.runtime.selectedId, "overlay")
            }
        }
        Label {
            text: !root.ui.runtime.ready ? qsTr("Start the core to view rules") : section.currentIndex === 0 ? qsTr("Filtering keeps the effective rule order unchanged. Source details and hit counts are limited by the core API.") : qsTr("Runtime metadata describes the active core. Selected profile snapshots may be pending apply. Updates run one at a time.")
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            objectName: "rules_status"
            visible: !!root.pageState.error || !!root.pageState.proxyError || !!root.pageState.ruleError || !!root.pageState.sampleAt
            text: (root.pageState.stale ? qsTr("Read failed; previous data retained") + " · " : "") + (root.pageState.error || "") + (root.pageState.proxyError ? qsTr("Proxy providers: %1").arg(root.pageState.proxyError) + " · " : "") + (root.pageState.ruleError ? qsTr("Rule providers: %1").arg(root.pageState.ruleError) + " · " : "") + (root.pageState.sampleAt ? qsTr("Read at %1").arg(root.pageState.sampleAt) : "")
            color: root.pageState.stale || root.pageState.error ? root.theme.error : root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }
        RuntimeList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            theme: root.theme
            heading: section.currentText
            listModel: section.currentIndex === 0 ? root.ui.liveRules : root.ui.liveProviders
            actionText: section.currentIndex === 1 ? qsTr("Update provider") : ""
            actionsEnabled: root.canRefresh
            describe: root.describeEntry
            onInvoked: function (entry) {
                root.ui.runtimeCommand("update_provider", [entry.kind, entry.name]);
            }
        }
    }
}
