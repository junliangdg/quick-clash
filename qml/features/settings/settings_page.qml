pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QuickClash.Ui

ScrollView {
    id: root
    objectName: "settings_page"
    required property UiState ui
    required property ThemeTokens theme
    contentWidth: availableWidth
    clip: true
    property var pendingNetworkArgs: []
    property var pendingBackupPreview: ({})
    property var pendingMaintenancePreview: ({})
    readonly property var settingsState: ui.runtime.settingsState || ({})
    function requestNetworkSettings(): void {
        pendingNetworkArgs = [outboundMode.model[outboundMode.currentIndex].key, mixedPort.value.toString(), controllerPort.value.toString(), dnsMode.model[dnsMode.currentIndex].key, ui.runtime.epoch || "", ui.runtime.selectionGeneration || "0"];
        networkConfirmation.open();
    }
    function networkSummary(value): string {
        return qsTr("Mode %1 · proxy port %2 · controller port %3 · DNS %4").arg(value.mode || "—").arg(value.mixedPort || "—").arg(value.controllerPort || "—").arg(value.dnsMode || "profile");
    }
    ThemedDialog {
        id: networkConfirmation
        objectName: "network_settings_confirmation"
        theme: root.theme
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 32)
        height: Math.min(360, parent.height - 32)
        implicitHeight: height
        title: qsTr("Apply network settings?")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ScrollView {
            id: networkConfirmationBody
            contentWidth: availableWidth
            clip: true
            Label {
                width: networkConfirmationBody.availableWidth
                wrapMode: Text.WordWrap
                color: root.theme.text
                text: root.networkSummary({
                    mode: root.pendingNetworkArgs[0],
                    mixedPort: root.pendingNetworkArgs[1],
                    controllerPort: root.pendingNetworkArgs[2],
                    dnsMode: root.pendingNetworkArgs[3]
                }) + "\n" + (root.ui.runtime.ready ? qsTr("The core restarts once and active connections are interrupted. If applying fails, QuickClash restores the confirmed settings.") : qsTr("These settings are saved for the next core session. They are not active while the core is stopped."))
            }
        }
        onAccepted: root.ui.runtimeCommand("network_settings", root.pendingNetworkArgs)
        onClosed: root.pendingNetworkArgs = []
    }
    ThemedDialog {
        id: geoConfirmation
        theme: root.theme
        title: qsTr("Download and apply Geo databases?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: Math.min(360, root.availableWidth - 40)
            wrapMode: Text.WordWrap
            text: qsTr("Downloads GeoIP and GeoSite from MetaCubeX over HTTPS, validates both with Clash Meta (mihomo), and applies them together. Existing connections restart. The previous snapshot is retained. TUN currently uses bundled Geo databases.")
        }
        onAccepted: root.ui.runtimeCommand("update_geo")
    }
    ThemedDialog {
        id: geoRollback
        theme: root.theme
        title: qsTr("Restore the previous Geo snapshot?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.ui.runtimeCommand("rollback_geo", ["confirmed"])
    }
    ThemedDialog {
        id: geoBundled
        theme: root.theme
        title: qsTr("Use the bundled Geo databases?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: Math.min(360, root.availableWidth - 40)
            wrapMode: Text.WordWrap
            text: qsTr("Validates the offline databases from the application package and applies them together. The current snapshot remains available for rollback.")
        }
        onAccepted: root.ui.runtimeCommand("bundled_geo", ["confirmed"])
    }
    FileDialog {
        id: restoreFile
        title: qsTr("Restore offline profiles")
        nameFilters: [qsTr("QuickClash backup (*.json)")]
        onAccepted: root.ui.runtimeCommand("preview_backup", [selectedFile.toString()])
    }
    Connections {
        target: root.ui
        function onRuntimeChanged() {
            const maintenance = (root.ui.runtime.features || {}).maintenancePreview;
            if (root.ui.uiVisible && maintenance && !maintenanceConfirmation.visible) {
                root.pendingMaintenancePreview = maintenance;
                maintenanceConfirmation.open();
            }
            const preview = (root.ui.runtime.features || {}).backupPreview;
            if (root.ui.uiVisible && preview && !restoreConfirmation.visible) {
                root.pendingBackupPreview = preview;
                restoreConfirmation.open();
            }
        }
        function onUiAboutToHide() {
            maintenanceConfirmation.close();
            root.ui.runtimeCommand("cancel_maintenance");
            restoreConfirmation.close();
            root.ui.runtimeCommand("cancel_backup_restore");
        }
    }
    Component.onDestruction: {
        root.ui.runtimeCommand("cancel_backup_restore");
        root.ui.runtimeCommand("cancel_maintenance");
    }
    ThemedDialog {
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
        title: root.pendingMaintenancePreview.kind === "diagnostics" ? qsTr("Export this support package?") : root.pendingMaintenancePreview.kind === "repair_preview" ? qsTr("Archive data and start fresh?") : qsTr("Review storage cleanup")
        contentItem: ScrollView {
            id: maintenanceBody
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: maintenanceBody.availableWidth
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.pendingMaintenancePreview.kind === "diagnostics" ? qsTr("Includes version, fixed settings, state and log counts. Excludes log messages, names, addresses, paths, URLs, credentials and connection targets. Nothing is uploaded.") : root.pendingMaintenancePreview.kind === "repair_preview" ? qsTr("Preserves all current data in a private recovery folder. Restart QuickClash to use an empty store, then import a compatible backup or configuration. Newer schemas are not converted. Recovery archives may contain plain text credentials; keep them private.") : qsTr("Budgets: 1 GiB total, 64 MiB temporary cache, 15 MiB exported logs. Removes old disposable files only when over budget. Referenced configurations, history, drafts, transactions, core caches, backups and recovery archives are retained. Protected data can keep usage above budget.")
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
        onClosed: root.pendingMaintenancePreview = ({})
    }
    ThemedDialog {
        id: backupConfirmation
        objectName: "backup_confirmation"
        theme: root.theme
        parent: Overlay.overlay
        title: qsTr("Create sensitive local backup?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: Math.min(560, parent.width - 32)
        height: Math.min(440, parent.height - 32)
        implicitHeight: height
        contentItem: ScrollView {
            id: backupBody
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: backupBody.availableWidth
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("The backup contains node credentials as plain text. It excludes saved subscription credentials, generated session secrets and system proxy leases. Original YAML may contain resource URLs. Keep it private.")
                }
                ThemedComboBox {
                    id: backupScope
                    Accessible.name: qsTr("Backup content")
                    theme: root.theme
                    Layout.fillWidth: true
                    model: [qsTr("Offline expanded profiles"), qsTr("Original YAML and overrides (includes resource URLs)")]
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Excludes drafts, history, settings and Geo files. Limit: 10 MiB of decoded content; stored JSON up to 16 MiB. Source backups include a saved provider snapshot for offline restoration.")
                }
            }
        }
        onAccepted: root.ui.runtimeCommand("backup", ["confirmed", backupScope.currentIndex === 1 ? "source" : "offline"])
    }
    ThemedDialog {
        id: restoreConfirmation
        objectName: "backup_restore_confirmation"
        theme: root.theme
        parent: Overlay.overlay
        title: qsTr("Import profiles from this backup?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: Math.min(560, parent.width - 32)
        height: Math.min(550, parent.height - 32)
        implicitHeight: height
        contentItem: ScrollView {
            id: restoreBody
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: restoreBody.availableWidth
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Verified profiles are added as new offline profiles. Existing profiles, running sessions and proxy settings are preserved.")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Contains node credentials. Scope: %1 · Decoded bytes: %2. Original YAML may include provider URLs. No history, drafts, Geo files, settings or old sessions are restored.").arg(root.pendingBackupPreview.scope === "source" ? qsTr("Original YAML and overrides") : qsTr("Offline expanded profiles")).arg(root.pendingBackupPreview.decodedBytes || "—")
                }
                Repeater {
                    model: root.pendingBackupPreview.profiles || []
                    delegate: Label {
                        required property var modelData
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                        text: qsTr("%1 · Overrides: %2 · Provider snapshot: %3").arg(modelData.name).arg(modelData.overrides ? qsTr("Yes") : qsTr("No")).arg(modelData.snapshot ? qsTr("Yes") : qsTr("No"))
                    }
                }
            }
        }
        onRejected: root.ui.runtimeCommand("cancel_backup_restore")
        onAccepted: root.ui.runtimeCommand("restore_backup", [root.pendingBackupPreview.token || "", "confirmed"])
        onClosed: root.pendingBackupPreview = ({})
    }
    ColumnLayout {
        width: root.availableWidth
        spacing: root.theme.gap
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: appearance.implicitHeight + 32
            ColumnLayout {
                id: appearance
                anchors.fill: parent
                anchors.margins: root.theme.panelPadding
                spacing: root.theme.gap
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("Appearance")
                    font.pixelSize: root.theme.bodySize
                    font.weight: Font.DemiBold
                    color: root.theme.text
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("Light and dark themes are built in. The system theme is resolved before the first frame.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: qsTr("Language follows the system: English or Chinese. Other languages use English. Restart after changing the system language.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    spacing: 8
                    Repeater {
                        model: [
                            {
                                "key": "system",
                                "label": qsTr("Follow system")
                            },
                            {
                                "key": "light",
                                "label": qsTr("Light")
                            },
                            {
                                "key": "dark",
                                "label": qsTr("Dark")
                            }
                        ]
                        delegate: ActionButton {
                            required property var modelData
                            theme: root.theme
                            text: modelData.label
                            selected: root.ui.themeMode === modelData.key
                            enabled: root.ui.settingsReady
                            onClicked: root.ui.themeMode = modelData.key
                        }
                    }
                }
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            visible: root.width < 600
            implicitHeight: moduleContent.implicitHeight + 32
            ColumnLayout {
                id: moduleContent
                anchors.fill: parent
                anchors.margins: root.theme.panelPadding
                spacing: 12
                Label {
                    text: qsTr("More pages")
                    color: root.theme.text
                    font.weight: Font.DemiBold
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: root.ui.navigation.filter(function (entry) {
                            return ["home", "proxies", "subscriptions", "settings"].indexOf(entry.pageId) < 0;
                        })
                        delegate: ActionButton {
                            required property var modelData
                            theme: root.theme
                            text: modelData.title
                            onClicked: root.ui.navigate(modelData.pageId)
                        }
                    }
                }
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: delaySettings.implicitHeight + 32
            ColumnLayout {
                id: delaySettings
                anchors.fill: parent
                anchors.margins: root.theme.panelPadding
                spacing: 12
                Label {
                    text: qsTr("Node delay testing")
                    font.pixelSize: root.theme.bodySize
                    font.weight: Font.DemiBold
                    color: root.theme.text
                }
                Label {
                    text: qsTr("This HTTPS target is used by individual and group node delay tests. Tests run only when requested.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                ThemedField {
                    id: delayTarget
                    theme: root.theme
                    objectName: "delay_target"
                    Layout.fillWidth: true
                    text: root.ui.runtime.delayTarget || "https://www.gstatic.com/generate_204"
                    placeholderText: qsTr("HTTPS target for node delay")
                    maximumLength: 8192
                }
                ActionButton {
                    theme: root.theme
                    text: qsTr("Save delay test target")
                    enabled: root.ui.settingsReady && ((root.ui.runtime.features || {}).delayTest || {}).state !== "Running"
                    onClicked: root.ui.runtimeCommand("delay_target", [delayTarget.text.trim()])
                }
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: repair.implicitHeight + 32
            ColumnLayout {
                id: repair
                anchors.fill: parent
                anchors.margins: root.theme.panelPadding
                spacing: 12
                Label {
                    text: qsTr("Network settings")
                    font.pixelSize: root.theme.bodySize
                    font.weight: Font.DemiBold
                    color: root.theme.text
                }
                Label {
                    text: (root.ui.runtimeError || qsTr("No runtime errors")) + (root.ui.runtime.readOnly ? qsTr(" · Data is read-only; back up and inspect damaged files") : "")
                    color: root.theme.text
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: qsTr("The network engine runs automatically and restarts after a crash. Turning off these switches keeps it available for local forwarding.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    objectName: "network_settings_state"
                    text: qsTr("Saved: %1").arg(root.networkSummary(root.settingsState.saved || {})) + "\n" + (root.settingsState.running ? qsTr("Active: %1").arg(root.networkSummary(root.settingsState.effective || {})) : qsTr("Core stopped · saved settings are not active")) + (root.settingsState.pending ? "\n" + qsTr("Applying requested settings: %1").arg(root.networkSummary(root.settingsState.requested || {})) : "")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: qsTr("System proxy · requested %1 · active %2; TUN · requested %3 · active %4").arg((root.ui.runtime.preferences || {}).systemProxyDesired ? qsTr("On") : qsTr("Off")).arg(root.ui.runtime.systemProxy ? qsTr("On") : qsTr("Off")).arg((root.ui.runtime.preferences || {}).tunDesired ? qsTr("On") : qsTr("Off")).arg(root.ui.runtime.tunState === "On" ? qsTr("On") : qsTr("Off"))
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                ThemedSwitch {
                    theme: root.theme
                    text: qsTr("System proxy")
                    objectName: "settings_system_proxy"
                    checked: !!root.ui.runtime.systemProxy
                    enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly && (root.ui.runtime.capabilities || {}).systemProxy !== "Unsupported"
                    onClicked: root.ui.runtimeCommand("system_proxy", [checked ? "true" : "false"])
                    palette.windowText: root.theme.text
                }
                ThemedSwitch {
                    theme: root.theme
                    text: qsTr("Virtual network adapter (TUN)")
                    objectName: "settings_tun"
                    checked: root.ui.runtime.tunState === "On"
                    enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly && (root.ui.runtime.capabilities || {}).tun !== "Unsupported"
                    onClicked: root.ui.runtimeCommand("tun", [checked ? "true" : "false"])
                    palette.windowText: root.theme.text
                }
                ThemedComboBox {
                    id: outboundMode
                    Accessible.name: qsTr("Routing mode")
                    objectName: "settings_outbound_mode"
                    theme: root.theme
                    Layout.fillWidth: true
                    textRole: "label"
                    model: [
                        {
                            key: "rule",
                            label: qsTr("Rule")
                        },
                        {
                            key: "global",
                            label: qsTr("Global")
                        },
                        {
                            key: "direct",
                            label: qsTr("Direct")
                        }
                    ]
                    currentIndex: Math.max(0, model.findIndex(entry => entry.key === ((root.ui.runtime.preferences || {}).mode || "rule")))
                    enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
                }
                GridLayout {
                    columns: root.availableWidth >= root.theme.scaledSize(600) ? 4 : root.availableWidth >= root.theme.scaledSize(360) ? 2 : 1
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Proxy port")
                        color: root.theme.text
                    }
                    ThemedSpinBox {
                        id: mixedPort
                        Accessible.name: qsTr("Proxy port")
                        theme: root.theme
                        objectName: "mixed_port"
                        from: 1024
                        to: 65535
                        editable: true
                        value: root.ui.runtime.preferences ? root.ui.runtime.preferences.mixedPort : 7890
                        locale: Qt.locale("C")
                    }
                    Label {
                        text: qsTr("Controller port")
                        color: root.theme.text
                    }
                    ThemedSpinBox {
                        id: controllerPort
                        Accessible.name: qsTr("Controller port")
                        theme: root.theme
                        objectName: "controller_port"
                        from: 1024
                        to: 65535
                        editable: true
                        value: root.ui.runtime.preferences ? root.ui.runtime.preferences.controllerPort : 9090
                        locale: Qt.locale("C")
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Apply network settings…")
                        objectName: "apply_network_settings"
                        enabled: root.ui.settingsReady && !root.ui.runtime.busy && !root.ui.runtime.readOnly
                        onClicked: root.requestNetworkSettings()
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Open data folder")
                        onClicked: Qt.openUrlExternally(root.ui.runtime.dataUrl || "")
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("View errors")
                        onClicked: root.ui.navigate("logs")
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Reapply proxy using current system settings")
                        width: Math.min(implicitWidth, parent.width)
                        visible: !!root.ui.runtime.proxyConflict
                        enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy
                        onClicked: root.ui.runtimeCommand("reapply_system_proxy")
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("View effective profile")
                        enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy
                        onClicked: root.ui.runtimeCommand("preview")
                    }
                }
                Label {
                    text: qsTr("Changing mode, profile, DNS or ports briefly restarts the core. After a forced exit, reopening restores proxy settings still owned by QuickClash. Changes made by other apps are preserved.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: qsTr("QuickClash manages loopback listeners and session secrets. DNS runs inside Clash Meta (mihomo). Providers are validated snapshots; Geo uses verified bundled resources.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                ThemedTextArea {
                    theme: root.theme
                    visible: !!root.ui.runtime.effectivePreview
                    text: root.ui.runtime.effectivePreview || ""
                    Accessible.name: qsTr("Effective profile preview")
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    Layout.fillWidth: true
                    Layout.maximumHeight: 320
                    color: root.theme.text
                }
            }
        }
        Label {
            visible: root.ui.storageError.length > 0
            text: qsTr("Settings cannot be saved: %1. Existing data is preserved. Check the data folder.").arg(root.ui.storageError)
            color: root.theme.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: advanced.implicitHeight + 32
            ColumnLayout {
                id: advanced
                anchors.fill: parent
                anchors.margins: root.theme.panelPadding
                spacing: 12
                Label {
                    text: qsTr("DNS and diagnostics")
                    color: root.theme.text
                    font.weight: Font.DemiBold
                }
                Label {
                    text: (root.ui.runtime.capabilities || {}).tun === "Unsupported" ? qsTr("System proxy and TUN are not available on this platform yet.") : qsTr("TUN requires the Windows service installed from a verified package. Run install_service_windows.ps1 in an administrator terminal, then launch the installed GUI as an ordinary user. The GUI never needs administrator rights.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                ThemedComboBox {
                    id: dnsMode
                    Accessible.name: qsTr("DNS mode")
                    theme: root.theme
                    Layout.fillWidth: true
                    model: [
                        {
                            key: "profile",
                            label: qsTr("Use profile DNS")
                        },
                        {
                            key: "fake-ip",
                            label: "fake-ip"
                        },
                        {
                            key: "redir-host",
                            label: "redir-host"
                        }
                    ]
                    textRole: "label"
                    currentIndex: Math.max(0, model.findIndex(entry => entry.key === ((root.ui.runtime.preferences || {}).dnsMode || "profile")))
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Review and apply network settings…")
                        enabled: root.ui.settingsReady && !root.ui.runtime.busy && !root.ui.runtime.readOnly
                        onClicked: root.requestNetworkSettings()
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Clear DNS cache")
                        enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy && (root.ui.runtime.features || {}).dnsResult !== "Clearing"
                        onClicked: root.ui.runtimeCommand("flush_dns")
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Update Geo databases…")
                        enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && root.ui.runtime.tunState !== "On"
                        onClicked: geoConfirmation.open()
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Roll back Geo databases…")
                        enabled: !!root.ui.runtime.selectedId && (root.ui.runtime.preferences || {}).previousGeoSnapshot !== undefined && !root.ui.runtime.busy && !root.ui.runtime.readOnly && root.ui.runtime.tunState !== "On"
                        onClicked: geoRollback.open()
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Use bundled Geo databases…")
                        enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && root.ui.runtime.tunState !== "On"
                        onClicked: geoBundled.open()
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Cancel Geo update")
                        visible: ["Downloading", "Validating"].indexOf((root.ui.runtime.features || {}).geoResult || "") >= 0
                        onClicked: root.ui.runtimeCommand("cancel_geo")
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Export safe diagnostics")
                        onClicked: root.ui.runtimeCommand("diagnostics")
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Open diagnostics")
                        visible: !!(root.ui.runtime.features || {}).exportUrl
                        onClicked: Qt.openUrlExternally(root.ui.runtime.features.exportUrl)
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Review storage cleanup")
                        enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
                        onClicked: root.ui.runtimeCommand("storage_preview")
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Archive damaged data…")
                        visible: !!root.ui.runtime.readOnly
                        enabled: !root.ui.runtime.ready && !root.ui.runtime.busy && !(root.ui.runtime.features || {}).recoveryArchiveUrl
                        onClicked: root.ui.runtimeCommand("repair_preview")
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Open recovery archive")
                        visible: !!(root.ui.runtime.features || {}).recoveryArchiveUrl
                        onClicked: Qt.openUrlExternally(root.ui.runtime.features.recoveryArchiveUrl)
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Back up profiles…")
                        enabled: !root.ui.runtime.busy
                        onClicked: backupConfirmation.open()
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Restore profiles…")
                        enabled: !root.ui.runtime.busy
                        onClicked: restoreFile.open()
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Open backup folder")
                        visible: !!(root.ui.runtime.features || {}).backupUrl
                        onClicked: Qt.openUrlExternally(root.ui.runtime.dataUrl + "/backups")
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Cancel profile restore")
                        visible: (root.ui.runtime.features || {}).backupResult === "Restoring"
                        onClicked: root.ui.runtimeCommand("cancel_backup_restore")
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: {
                        const features = root.ui.runtime.features || {};
                        if (features.maintenanceResult === "RestartRequired")
                            return qsTr("Data archived. Restart QuickClash to finish recovery. The archive is never removed automatically.");
                        const result = features.storageCleanup;
                        return result ? qsTr("Removed %1 files · Reclaimed %2 bytes").arg(result.removedFiles).arg(result.reclaimedBytes) + (result.budgetSatisfied ? "" : " · " + qsTr("Protected data still exceeds the storage budget.")) : "";
                    }
                }
                Label {
                    text: {
                        const status = (root.ui.runtime.features || {}).dnsResult || "";
                        const labels = {
                            Clearing: qsTr("Clearing DNS cache…"),
                            Cleared: qsTr("DNS cache cleared"),
                            Failed: qsTr("DNS cache could not be cleared")
                        };
                        const geoLabels = {
                            Downloading: qsTr("Downloading both Geo databases…"),
                            Validating: qsTr("Validating Geo databases…"),
                            Applying: qsTr("Applying Geo databases…"),
                            Applied: qsTr("Geo snapshot saved"),
                            Unchanged: qsTr("Geo databases are unchanged"),
                            Failed: qsTr("Geo update failed; previous snapshot kept"),
                            Cancelled: qsTr("Geo update cancelled")
                        };
                        const backupLabels = {
                            Saved: qsTr("Sensitive backup saved"),
                            Preview: qsTr("Review the backup before importing"),
                            Restoring: qsTr("Validating profiles for restore…"),
                            Restored: qsTr("Profiles restored"),
                            Failed: qsTr("Profile restore failed; existing profiles kept"),
                            Cancelled: qsTr("Profile restore cancelled")
                        };
                        return (labels[status] || "") + " · " + (backupLabels[(root.ui.runtime.features || {}).backupResult] || "") + " · " + (geoLabels[(root.ui.runtime.features || {}).geoResult] || "");
                    }
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: lifecycle.implicitHeight + 32
            ColumnLayout {
                id: lifecycle
                anchors.fill: parent
                anchors.margins: root.theme.panelPadding
                spacing: 10
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("Window and tray")
                    font.pixelSize: root.theme.bodySize
                    font.weight: Font.DemiBold
                    color: root.theme.text
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: root.ui.trayAvailable ? qsTr("Close to the tray; confirmation is required the first time.") : qsTr("No tray is available. Closing keeps the window open so you can minimize or quit.")
                    color: root.theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                ThemedCheckBox {
                    id: unloadOption
                    theme: root.theme
                    font.family: root.theme.fontFamily
                    text: qsTr("Low memory: unload after 5 seconds hidden or minimized (experimental)")
                    Layout.fillWidth: true
                    contentItem: Text {
                        text: unloadOption.text
                        font: unloadOption.font
                        color: root.theme.text
                        leftPadding: unloadOption.indicator.width + unloadOption.spacing
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                    }
                    checked: root.ui.unloadWhenHidden
                    enabled: root.ui.settingsReady && (root.ui.trayAvailable || checked)
                    onToggled: root.ui.unloadWhenHidden = checked
                }
                Label {
                    text: qsTr("Off by default. Reuses your saved unload preference. Requires a working tray; without one, the minimized taskbar window is kept. The engine and subscription updates keep running; opening the tray rebuilds the interface.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("State %1 · Active views %2 · Streams %3").arg(root.ui.hostStatus).arg(root.ui.activeDemandCount).arg(root.ui.activeStreamCount)
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: replay.implicitHeight + 32
            ColumnLayout {
                id: replay
                anchors.fill: parent
                anchors.margins: root.theme.panelPadding
                spacing: 10
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("Prototype data")
                    font.pixelSize: root.theme.bodySize
                    font.weight: Font.DemiBold
                    color: root.theme.text
                }
                ThemedCheckBox {
                    theme: root.theme
                    font.family: root.theme.fontFamily
                    text: qsTr("Replay 500 nodes / %1 connections").arg(root.ui.replayConnections)
                    checked: root.ui.replay
                    onToggled: root.ui.replay = checked
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("Synthetic records preview the interface without starting the core or changing the system proxy.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
