pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QuickClash.Ui

ColumnLayout {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    spacing: theme.gap
    objectName: "settings_data"
    readonly property var features: ui.runtime.features || ({})
    readonly property Item recoveryAnchor: recoveryGroup
    property var pendingBackupPreview: ({})
    property var pendingMaintenancePreview: ({})
    property string lastBackupToken: ""
    property string lastMaintenanceToken: ""
    function openDialog(loader): void {
        loader.active = true;
        loader.item.open();
    }
    function syncPreviews(): void {
        if (!ui.uiVisible)
            return;
        const maintenance = features.maintenancePreview;
        if (maintenance && maintenance.token !== lastMaintenanceToken) {
            lastMaintenanceToken = maintenance.token;
            pendingMaintenancePreview = maintenance;
            openDialog(maintenanceDialog);
        }
        const preview = features.backupPreview;
        if (preview && preview.token !== lastBackupToken) {
            lastBackupToken = preview.token;
            pendingBackupPreview = preview;
            openDialog(restoreDialog);
        }
        if (ui.runtime.effectivePreview && !effectiveDialog.active)
            openDialog(effectiveDialog);
    }
    function releasePreviews(): void {
        for (const loader of [backupDialog, restoreDialog, maintenanceDialog, restoreFileLoader, effectiveDialog])
            loader.active = false;
        pendingBackupPreview = ({});
        pendingMaintenancePreview = ({});
        ui.runtimeCommand("discard_backup_preview");
        ui.runtimeCommand("cancel_maintenance");
        ui.runtimeCommand("clear_preview");
    }
    Connections {
        target: root.ui
        function onRuntimeChanged(): void {
            root.syncPreviews();
        }
        function onUiAboutToHide(): void {
            root.releasePreviews();
        }
    }
    Component.onDestruction: releasePreviews()
    SettingsGroup {
        theme: root.theme
        title: qsTranslate("settings_page", "Backup and restore")
        Layout.fillWidth: true
        Label {
            text: qsTranslate("settings_page", "Back up profiles and rules. Application settings are excluded. Backups can contain plain text credentials; keep them private.")
            color: root.theme.muted
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 12
            spacing: 8
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Back up profiles and rules…")
                enabled: !root.ui.runtime.busy
                onClicked: root.openDialog(backupDialog)
            }
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Restore profiles…")
                enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
                onClicked: root.openDialog(restoreFileLoader)
            }
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Open backup folder")
                visible: !!(root.ui.runtime.features || {}).backupUrl
                onClicked: Qt.openUrlExternally(root.ui.runtime.dataUrl + "/backups")
            }
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Cancel profile restore")
                visible: (root.ui.runtime.features || {}).backupResult === "Restoring"
                onClicked: root.ui.runtimeCommand("cancel_backup_restore")
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            color: root.theme.muted
            wrapMode: Text.WordWrap
            text: root.features.backupResult === "Failed" && root.ui.runtime.error === "UserRuleBackupUnavailable" ? qsTranslate("settings_page", "Backup cannot preserve a custom rule's provider or group in an offline profile. Review the profile's custom rules first.") : ({
                    Saved: qsTranslate("settings_page", "Sensitive backup saved"),
                    Preview: qsTranslate("settings_page", "Review the backup before importing"),
                    Restoring: qsTranslate("settings_page", "Validating profiles for restore…"),
                    Restored: qsTranslate("settings_page", "Profiles restored"),
                    RulesApplying: qsTranslate("settings_page", "Applying global rules…"),
                    RulesRestored: qsTranslate("settings_page", "Global rules restored"),
                    RulesFailed: qsTranslate("settings_page", "Global rules could not be applied; existing rules kept"),
                    Failed: qsTranslate("settings_page", "Profile restore failed; existing profiles kept"),
                    Cancelled: qsTranslate("settings_page", "Profile restore cancelled")
                })[root.features.backupResult] || ""
            visible: text.length > 0
        }
    }
    SettingsGroup {
        theme: root.theme
        title: qsTranslate("settings_page", "Local data")
        Layout.fillWidth: true
        Flow {
            Layout.fillWidth: true
            spacing: 8
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Open data folder")
                onClicked: Qt.openUrlExternally(root.ui.runtime.dataUrl || "")
            }
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Review storage cleanup")
                enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
                onClicked: root.ui.runtimeCommand("storage_preview")
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            color: root.theme.muted
            wrapMode: Text.WordWrap
            text: root.features.storageCleanup ? qsTranslate("settings_page", "Removed %1 files · Reclaimed %2 bytes").arg(root.features.storageCleanup.removedFiles).arg(root.features.storageCleanup.reclaimedBytes) + (root.features.storageCleanup.budgetSatisfied ? "" : " · " + qsTranslate("settings_page", "Protected data still exceeds the storage budget.")) : ""
        }
    }
    SettingsGroup {
        theme: root.theme
        title: qsTranslate("settings_page", "Diagnostics")
        Layout.fillWidth: true
        Flow {
            Layout.fillWidth: true
            spacing: 8
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Export safe diagnostics")
                onClicked: root.ui.runtimeCommand("diagnostics")
            }
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Open diagnostics")
                visible: !!(root.ui.runtime.features || {}).exportUrl
                onClicked: Qt.openUrlExternally(root.ui.runtime.features.exportUrl)
            }
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "View errors")
                onClicked: root.ui.navigate("logs")
            }
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "View effective profile")
                enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy
                onClicked: root.ui.runtimeCommand("preview")
            }
        }
    }
    SettingsGroup {
        id: recoveryGroup
        objectName: "settings_recovery"
        theme: root.theme
        title: qsTranslate("settings_page", "Recovery")
        visible: !!root.ui.runtime.readOnly || !!root.features.recoveryArchiveUrl
        Layout.fillWidth: true
        Flow {
            Layout.fillWidth: true
            spacing: 8
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Archive damaged data…")
                visible: !!root.ui.runtime.readOnly
                enabled: !root.ui.runtime.ready && !root.ui.runtime.busy && !(root.ui.runtime.features || {}).recoveryArchiveUrl
                onClicked: root.ui.runtimeCommand("repair_preview")
            }
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Open recovery archive")
                visible: !!(root.ui.runtime.features || {}).recoveryArchiveUrl
                onClicked: Qt.openUrlExternally(root.ui.runtime.features.recoveryArchiveUrl)
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            color: root.theme.warning
            wrapMode: Text.WordWrap
            text: root.features.maintenanceResult === "RestartRequired" ? qsTranslate("settings_page", "Data archived. Restart QuickClash to finish recovery. The archive is never removed automatically.") : qsTranslate("settings_page", "Recovery archives may contain credentials. Stop the core and finish active operations before archiving.")
        }
    }

    Loader {
        id: maintenanceDialog
        objectName: "settings_loader_maintenanceDialog"
        active: false
        sourceComponent: ThemedDialog {
            id: maintenanceConfirmation
            objectName: "maintenance_confirmation"
            theme: root.theme
            parent: Overlay.overlay
            anchors.centerIn: parent
            width: Math.min(560, parent.width - 32)
            height: Math.min(540, parent.height - 32)
            implicitHeight: height
            modal: true
            standardButtons: Dialog.Ok | Dialog.Cancel
            title: root.pendingMaintenancePreview.kind === "diagnostics" ? qsTranslate("settings_page", "Export this support package?") : root.pendingMaintenancePreview.kind === "repair_preview" ? qsTranslate("settings_page", "Archive data and start fresh?") : qsTranslate("settings_page", "Review storage cleanup")
            contentItem: ThemedScrollView {
                id: maintenanceBody
                theme: root.theme
                form: maintenanceBodyForm
                ColumnLayout {
                    id: maintenanceBodyForm
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: root.pendingMaintenancePreview.kind === "diagnostics" ? qsTranslate("settings_page", "Includes version, fixed settings, state and log counts. Excludes log messages, names, addresses, paths, URLs, credentials and connection targets. Nothing is uploaded.") : root.pendingMaintenancePreview.kind === "repair_preview" ? qsTranslate("settings_page", "Preserves all current data in a private recovery folder. Restart QuickClash to use an empty store, then import a compatible backup or configuration. Newer schemas are not converted. Recovery archives may contain plain text credentials; keep them private.") : qsTranslate("settings_page", "Budgets: 1 GiB total, 64 MiB temporary cache, 15 MiB exported logs. Removes old disposable files only when over budget. Referenced configurations, history, drafts, transactions, core caches, backups and recovery archives are retained. Protected data can keep usage above budget.")
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                        text: root.pendingMaintenancePreview.text || ""
                    }
                }
            }
            onAccepted: root.ui.runtimeCommand("confirm_maintenance", [root.pendingMaintenancePreview.token, root.pendingMaintenancePreview.kind])
            onRejected: root.ui.runtimeCommand("cancel_maintenance")
            onClosed: {
                root.pendingMaintenancePreview = ({});
                maintenanceDialog.active = false;
            }
        }
    }

    Loader {
        id: backupDialog
        objectName: "settings_loader_backupDialog"
        active: false
        sourceComponent: ThemedDialog {
            id: backupConfirmation
            objectName: "backup_confirmation"
            theme: root.theme
            parent: Overlay.overlay
            title: qsTranslate("settings_page", "Create sensitive local backup?")
            modal: true
            anchors.centerIn: parent
            standardButtons: Dialog.Ok | Dialog.Cancel
            width: Math.min(560, parent.width - 32)
            height: Math.min(440, parent.height - 32)
            implicitHeight: height
            contentItem: ThemedScrollView {
                id: backupBody
                theme: root.theme
                form: backupBodyForm
                ColumnLayout {
                    id: backupBodyForm
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTranslate("settings_page", "The backup contains node credentials as plain text. It excludes saved subscription credentials, generated session secrets and system proxy leases. Original YAML may contain resource URLs. Keep it private.")
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTranslate("settings_page", "Includes global rules, profile rules and proxy group bindings. Without profiles, only global rules are backed up.")
                    }
                    ThemedComboBox {
                        id: backupScope
                        Accessible.name: qsTranslate("settings_page", "Backup content")
                        theme: root.theme
                        Layout.fillWidth: true
                        model: [qsTranslate("settings_page", "Offline expanded profiles"), qsTranslate("settings_page", "Original YAML and overrides (includes resource URLs)")]
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTranslate("settings_page", "Excludes drafts, history, settings and Geo files. Limit: 10 MiB of decoded content; stored JSON up to 16 MiB. Source backups include a saved provider snapshot for offline restoration.")
                    }
                }
            }
            onAccepted: root.ui.runtimeCommand("backup", ["confirmed", backupScope.currentIndex === 1 ? "source" : "offline"])
            onClosed: backupDialog.active = false
        }
    }

    Loader {
        id: restoreDialog
        objectName: "settings_loader_restoreDialog"
        active: false
        sourceComponent: ThemedDialog {
            id: restoreConfirmation
            objectName: "backup_restore_confirmation"
            readonly property bool globalOnly: (root.pendingBackupPreview.profiles || []).length === 0
            theme: root.theme
            parent: Overlay.overlay
            title: globalOnly ? qsTranslate("settings_page", "Restore global rules from this backup?") : qsTranslate("settings_page", "Import profiles from this backup?")
            modal: true
            anchors.centerIn: parent
            standardButtons: Dialog.Ok | Dialog.Cancel
            width: Math.min(560, parent.width - 32)
            height: Math.min(550, parent.height - 32)
            implicitHeight: height
            onOpened: {
                restoreGlobalRules.checked = false;
                standardButton(Dialog.Ok).objectName = "backup_restore_accept";
                standardButton(Dialog.Ok).enabled = Qt.binding(function () {
                    return !restoreConfirmation.globalOnly || restoreGlobalRules.checked;
                });
            }
            contentItem: ThemedScrollView {
                id: restoreBody
                theme: root.theme
                form: restoreBodyForm
                ColumnLayout {
                    id: restoreBodyForm
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: restoreConfirmation.globalOnly ? qsTranslate("settings_page", "This backup contains only global rules. Select the checkbox to restore them.") : qsTranslate("settings_page", "Verified profiles are added as new offline profiles. Existing profiles, running sessions and proxy settings are preserved.")
                    }
                    ThemedCheckBox {
                        id: restoreGlobalRules
                        objectName: "backup_restore_global_rules"
                        theme: root.theme
                        Layout.fillWidth: true
                        visible: !!root.pendingBackupPreview.hasGlobalRules
                        text: restoreConfirmation.globalOnly ? qsTranslate("settings_page", "Restore global rules (may restart the core)") : qsTranslate("settings_page", "Also restore global rules (may restart the core)")
                    }
                    Label {
                        visible: restoreGlobalRules.visible
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTranslate("settings_page", "By default, existing global rules are kept. Restoring them replaces the current global rules and is applied separately.")
                    }
                    Label {
                        visible: !restoreConfirmation.globalOnly
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTranslate("settings_page", "Contains node credentials. Scope: %1 · Decoded bytes: %2. Original YAML may include provider URLs. No history, drafts, Geo files, settings or old sessions are restored.").arg(root.pendingBackupPreview.scope === "source" ? qsTranslate("settings_page", "Original YAML and overrides") : qsTranslate("settings_page", "Offline expanded profiles")).arg(root.pendingBackupPreview.decodedBytes || "—")
                    }
                    Repeater {
                        model: root.pendingBackupPreview.profiles || []
                        delegate: Label {
                            required property var modelData
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            text: qsTranslate("settings_page", "%1 · Overrides: %2 · Provider snapshot: %3").arg(modelData.name).arg(modelData.overrides ? qsTranslate("settings_page", "Yes") : qsTranslate("settings_page", "No")).arg(modelData.snapshot ? qsTranslate("settings_page", "Yes") : qsTranslate("settings_page", "No"))
                        }
                    }
                }
            }
            onRejected: root.ui.runtimeCommand("cancel_backup_restore")
            onAccepted: root.ui.runtimeCommand("restore_backup", [root.pendingBackupPreview.token || "", "confirmed", restoreGlobalRules.checked ? "global_rules" : ""])
            onClosed: {
                root.pendingBackupPreview = ({});
                restoreDialog.active = false;
            }
        }
    }
    Loader {
        id: restoreFileLoader
        active: false
        sourceComponent: FileDialog {
            title: qsTranslate("settings_page", "Restore offline profiles")
            nameFilters: [qsTranslate("settings_page", "QuickClash backup (*.json)")]
            onAccepted: {
                root.ui.runtimeCommand("preview_backup", [selectedFile.toString()]);
                restoreFileLoader.active = false;
            }
            onRejected: restoreFileLoader.active = false
        }
    }
    Loader {
        id: effectiveDialog
        objectName: "settings_loader_effectiveDialog"
        active: false
        sourceComponent: ThemedDialog {
            theme: root.theme
            parent: Overlay.overlay
            anchors.centerIn: parent
            width: Math.min(640, parent.width - 32)
            height: Math.min(600, parent.height - 32)
            title: qsTranslate("settings_page", "Effective profile preview")
            modal: true
            standardButtons: Dialog.Close
            contentItem: ThemedScrollView {
                theme: root.theme
                form: previewText
                ThemedTextArea {
                    id: previewText
                    theme: root.theme
                    Accessible.name: qsTranslate("settings_page", "Effective profile preview")
                    text: root.ui.runtime.effectivePreview || ""
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                }
            }
            onClosed: {
                root.ui.runtimeCommand("clear_preview");
                effectiveDialog.active = false;
            }
        }
    }
}
