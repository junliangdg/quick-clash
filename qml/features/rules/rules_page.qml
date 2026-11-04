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
        { "title": qsTr("Type"), "weight": 1.2 },
        { "title": qsTr("Payload"), "weight": 2.5 },
        { "title": qsTr("Proxy"), "weight": 1.35 },
        { "title": qsTr("Source"), "weight": 1.1 }
    ]
    readonly property var providerColumns: [
        { "title": qsTr("Provider"), "weight": 2.2 },
        { "title": qsTr("Type"), "weight": 1.4 },
        { "title": qsTr("Items"), "weight": 0.6, "numeric": true },
        { "title": qsTr("Updated"), "weight": 1.6 }
    ]
    function syncDemand(): void {
        const stream = customRulesDialog.visible ? "user_rules" : section.currentIndex === 0 ? "rules" : "providers";
        ui.setDemand(demandKey, stream, active);
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
    function sourceDescription(entry): string {
        if (entry.customRule)
            return entry.customRuleScope === "global" ? qsTr("Global custom rule") : qsTr("Profile custom rule");
        if (entry.providerName)
            return qsTr("Rule provider: %1").arg(entry.providerName);
        return qsTr("Effective configuration");
    }
    function describeEntry(entry): string {
        const count = typeof entry.count === "number" ? String(entry.count) : qsTr("Unknown");
        const updated = entry.updatedAt || qsTr("Unknown");
        if (section.currentIndex === 0) {
            return "#" + (entry.position || "?") + " · " + (entry.type || "") + " · " + (entry.payload || "") + " → " + (entry.proxy || "") + "\n" + sourceDescription(entry) + (entry.providerName ? " · " + qsTr("Items: %1 · Updated: %2").arg(count).arg(updated) + " · " + qsTr("Limited information") : "");
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
            case 4:
                return entry.customRule ? (entry.customRuleScope === "global" ? qsTr("Global custom rule") : qsTr("Profile custom rule")) : entry.providerName ? qsTr("Provider rule") : qsTr("Configuration rule");
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
        if (section.currentIndex === 0 && index === 4) {
            if (entry.customRule)
                return theme.accent;
            if (entry.providerName)
                return theme.warning;
            return theme.muted;
        }
        if (section.currentIndex === 1 && index === 3)
            return entry.stale ? theme.warning : theme.muted;
        return theme.text;
    }
    onActiveChanged: {
        if (!active && customRulesDialog.visible) {
            customRulesDialog.preserveDraftOnClose = true;
            customRulesDialog.close();
        }
        syncDemand();
    }
    Component.onCompleted: syncDemand()
    Component.onDestruction: ui.setDemand(demandKey, "rules", false)

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label {
                text: section.currentIndex === 0 ? qsTr("Rules · %1").arg(root.ui.liveRules.count) : qsTr("Providers · %1").arg(root.ui.liveProviders.count)
                color: root.theme.text
                font.pixelSize: root.theme.bodySize
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
            }
            ActionButton {
                id: customRulesButton
                objectName: "open_custom_rules"
                theme: root.theme
                Layout.maximumWidth: root.width * 0.64
                text: qsTr("Manage custom rules…")
                prominent: true
                insetFocus: true
                ThemedToolTip {
                    theme: root.theme
                    visible: customRulesButton.hovered || customRulesButton.activeFocus
                    text: customRulesButton.text
                }
                contentItem: Text {
                    text: customRulesButton.text
                    font: customRulesButton.font
                    color: customRulesButton.foregroundColor
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: customRulesDialog.open()
            }
        }
        RowLayout {
            id: section
            objectName: "rules_section"
            property int currentIndex: 0
            Layout.fillWidth: true
            spacing: 6
            ActionButton {
                theme: root.theme
                Layout.fillWidth: true
                text: qsTr("Rules")
                selected: section.currentIndex === 0
                onClicked: section.currentIndex = 0
            }
            ActionButton {
                theme: root.theme
                Layout.fillWidth: true
                text: qsTr("Providers")
                selected: section.currentIndex === 1
                onClicked: section.currentIndex = 1
            }
            onCurrentIndexChanged: root.syncDemand()
        }
        GridLayout {
            Layout.fillWidth: true
            columns: width >= root.theme.scaledSize(620) ? 2 : 1
            columnSpacing: 8
            rowSpacing: 8
            ActionButton {
                theme: root.theme
                objectName: "refresh_rules"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.pageState.loading ? qsTr("Loading…") : qsTr("Refresh")
                enabled: root.canRefresh
                onClicked: root.ui.runtimeCommand(section.currentIndex === 0 ? "rules" : "providers")
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
            minimumWidth: root.theme.scaledSize(720)
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

    ThemedDialog {
        id: customRulesDialog
        objectName: "custom_rules_dialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        theme: root.theme
        modal: true
        title: qsTr("Custom rules")
        property bool preserveDraftOnClose: false
        width: Math.max(0, Math.min(root.theme.scaledSize(920), parent ? parent.width - 48 : root.theme.scaledSize(920)))
        height: Math.max(0, Math.min(root.theme.scaledSize(760), parent ? parent.height - 48 : root.theme.scaledSize(760)))
        implicitHeight: height
        padding: 12
        closePolicy: rulesEditor.saving ? Popup.NoAutoClose : Popup.CloseOnEscape
        standardButtons: Dialog.NoButton
        initialFocusItem: confirmCustomRules
        header: Label {
            objectName: "custom_rules_title"
            text: customRulesDialog.title
            padding: 12
            color: root.theme.text
            font.pixelSize: root.theme.scaledSize(18)
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        contentItem: UserRulesEditor {
            id: rulesEditor
            ui: root.ui
            theme: root.theme
            onSaveCompleted: customRulesDialog.close()
        }
        footer: Item {
            id: customRulesFooter
            objectName: "custom_rules_footer"
            width: rulesEditor.width
            implicitWidth: width
            implicitHeight: footerLayout.implicitHeight + 16

            GridLayout {
                id: footerLayout
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 4
                anchors.bottomMargin: 12
                readonly property bool inlineHint: width >= Math.min(confirmHint.implicitWidth, root.theme.scaledSize(100)) + cancelCustomRules.implicitWidth + confirmCustomRules.implicitWidth + 2 * columnSpacing
                columns: 3
                columnSpacing: 8
                rowSpacing: 8
                Label {
                    id: confirmHint
                    objectName: "custom_rules_confirm_hint"
                    text: qsTr("Changes are saved and applied when you confirm.")
                    color: root.theme.muted
                    font.pixelSize: root.theme.supportingSize
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.columnSpan: footerLayout.inlineHint ? 1 : 3
                }
                Item {
                    visible: !footerLayout.inlineHint
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
                ActionButton {
                    id: confirmCustomRules
                    objectName: "confirm_custom_rules"
                    theme: root.theme
                    prominent: true
                    text: qsTr("Confirm")
                    leftPadding: 8
                    rightPadding: 8
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: Math.min(implicitWidth, (footerLayout.width - 2 * footerLayout.columnSpacing) / 2)
                    Layout.maximumWidth: (footerLayout.width - 2 * footerLayout.columnSpacing) / 2
                    enabled: rulesEditor.canConfirm
                    contentItem: Text {
                        text: confirmCustomRules.text
                        font: confirmCustomRules.font
                        color: confirmCustomRules.foregroundColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    onClicked: rulesEditor.confirm()
                }
                ActionButton {
                    id: cancelCustomRules
                    theme: root.theme
                    objectName: "cancel_custom_rules"
                    text: qsTr("Cancel")
                    leftPadding: 8
                    rightPadding: 8
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: Math.min(implicitWidth, (footerLayout.width - 2 * footerLayout.columnSpacing) / 2)
                    Layout.maximumWidth: (footerLayout.width - 2 * footerLayout.columnSpacing) / 2
                    enabled: !rulesEditor.saving
                    contentItem: Text {
                        text: cancelCustomRules.text
                        font: cancelCustomRules.font
                        color: cancelCustomRules.foregroundColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    onClicked: {
                        rulesEditor.discardDraft();
                        customRulesDialog.close();
                    }
                }
            }
        }
        onRejected: {
            if (root.ui.uiVisible && !preserveDraftOnClose)
                rulesEditor.discardDraft();
        }
        onVisibleChanged: {
            if (!visible && root.ui.uiVisible && !preserveDraftOnClose)
                rulesEditor.discardDraft();
            preserveDraftOnClose = false;
            root.syncDemand();
        }
    }
}
