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
    readonly property RuntimeListModel pageModel: section.currentIndex === 0 ? ui.liveRules : ui.liveProviders
    readonly property var ruleColumns: [
        { "title": "#", "weight": 0.45, "numeric": true },
        { "title": qsTr("Type"), "weight": 1.3 },
        { "title": qsTr("Payload"), "weight": 2.8 },
        { "title": qsTr("Proxy"), "weight": 1.5 }
    ]
    readonly property var providerColumns: [
        { "title": qsTr("Provider"), "weight": 2.2 },
        { "title": qsTr("Type"), "weight": 1.4 },
        { "title": qsTr("Items"), "weight": 0.6, "numeric": true },
        { "title": qsTr("Updated"), "weight": 1.6 }
    ]
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
    function cellText(entry, index): string {
        if (section.currentIndex === 0) {
            switch (index) {
            case 0:
                return entry.position !== undefined ? String(entry.position) : "—";
            case 1:
                return entry.type || "—";
            case 2:
                return entry.payload || "—";
            case 3:
                return entry.proxy || "—";
            }
            return "";
        }
        switch (index) {
        case 0:
            return entry.name || "—";
        case 1:
            return [entry.mode === "runtime" ? qsTr("Core runtime") : qsTr("Profile snapshot"), entry.kind, entry.type, entry.behavior].filter(part => !!part).join(" · ");
        case 2:
            return typeof entry.count === "number" ? String(entry.count) : qsTr("Unknown");
        case 3:
            return entry.updatedAt || qsTr("Unknown");
        }
        return "";
    }
    function rowColor(entry, index): color {
        if (section.currentIndex === 1 && index === 3)
            return entry.stale ? theme.warning : theme.muted;
        return theme.text;
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
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                text: (section.currentIndex === 0 ? qsTr("Rules") : qsTr("Providers")) + " · " + root.pageModel.count
                color: root.theme.text
                font.pixelSize: root.theme.bodySize
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Label {
                text: !root.ui.runtime.ready ? qsTr("Start the core to view rules")
                    : section.currentIndex === 0 ? qsTr("Effective order") : qsTr("One update at a time")
                color: root.theme.muted
                font.pixelSize: root.theme.supportingSize
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: width >= root.theme.scaledSize(720) ? 4 : width >= root.theme.scaledSize(420) ? 2 : 1
            columnSpacing: 8
            rowSpacing: 8
            ThemedComboBox {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
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
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.pageState.loading ? qsTr("Loading…") : qsTr("Refresh")
                enabled: root.canRefresh
                onClicked: root.ui.runtimeCommand(section.currentIndex === 0 ? "rules" : "providers")
            }
            ActionButton {
                theme: root.theme
                objectName: "edit_rules"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: qsTr("Edit overrides…")
                enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly
                onClicked: editor.edit(root.ui.runtime.selectedId, "overlay")
            }
            RuntimeFilter {
                id: ruleFilter
                theme: root.theme
                listModel: root.pageModel
                placeholderText: section.currentIndex === 0 ? qsTr("Type, payload or proxy") : qsTr("Provider or type")
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
        }
        Label {
            objectName: "rules_status"
            visible: text.length > 0
            text: (root.pageState.stale ? qsTr("Read failed; previous data retained") + " · " : "") + (root.pageState.error || "") + (root.pageState.proxyError ? qsTr("Proxy providers: %1").arg(root.pageState.proxyError) + " · " : "") + (root.pageState.ruleError ? qsTr("Rule providers: %1").arg(root.pageState.ruleError) + " · " : "") + (root.pageState.sampleAt ? qsTr("Read at %1").arg(root.pageState.sampleAt) : "")
            color: root.pageState.stale || root.pageState.error ? root.theme.error : root.theme.muted
            font.pixelSize: root.theme.supportingSize
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }
        RuntimeList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            theme: root.theme
            listModel: root.pageModel
            columns: section.currentIndex === 0 ? root.ruleColumns : root.providerColumns
            rowHeight: root.theme.rowHeight
            minimumWidth: root.theme.scaledSize(620)
            cellText: root.cellText
            cellColor: root.rowColor
            describe: root.describeEntry
            expandable: section.currentIndex === 0
            activateRow: section.currentIndex === 0
            actionText: section.currentIndex === 0 ? "" : qsTr("Update")
            actionsEnabled: root.canRefresh
            onInvoked: function (entry) {
                if (section.currentIndex === 1)
                    root.ui.runtimeCommand("update_provider", [entry.kind, entry.name]);
            }
        }
    }
}
