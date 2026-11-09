pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ColumnLayout {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    spacing: theme.gap
    objectName: "settings_network"
    readonly property var session: ui.settingsSession
    readonly property var draft: session.network || ({})
    readonly property var settingsState: ui.runtime.settingsState || ({})
    readonly property bool showComparison: !settingsState.running || !!settingsState.pending || networkSummary(settingsState.saved || {}) !== networkSummary(settingsState.effective || {})
    property var pendingNetworkArgs: []
    property var portCandidate: ({})
    property string portEpoch: ""
    property string portGeneration: ""
    property string confirmationError: ""
    property bool tunHelpExpanded: false
    readonly property bool editable: ui.settingsReady && !ui.runtime.readOnly && !ui.runtime.busy && !session.networkPending
    function networkSummary(value): string {
        return qsTranslate("settings_page", "Mode %1 · proxy port %2 · controller port %3 · DNS %4").arg(value.mode || "—").arg(value.mixedPort || "—").arg(value.controllerPort || "—").arg(value.dnsMode || "profile");
    }
    function tunStatus(value): string {
        const labels = {
            On: qsTranslate("settings_page", "On"),
            Off: qsTranslate("settings_page", "Off"),
            AuthorizationRequired: qsTranslate("settings_page", "Authorization required"),
            Blocked: qsTranslate("settings_page", "Blocked"),
            Uncertain: qsTranslate("settings_page", "Cleanup is uncertain"),
            Missing: qsTranslate("settings_page", "Adapter missing")
        };
        return labels[value] || qsTranslate("settings_page", "Unknown");
    }
    function requestNetworkSettings(): void {
        pendingNetworkArgs = ui.prepareNetworkSettings();
        if (pendingNetworkArgs.length === 0)
            return;
        confirmationError = "";
        networkDialog.active = true;
        const dialog = networkDialog.item as ThemedDialog;
        if (dialog)
            dialog.open();
    }
    function releasePreviews(): void {
        networkDialog.active = false;
        portDialog.active = false;
        pendingNetworkArgs = [];
        portCandidate = ({});
        root.ui.runtimeCommand("cancel_listener_ports");
    }
    Connections {
        target: root.ui
        function onUiAboutToHide(): void {
            root.releasePreviews();
        }
        function onRuntimeChanged(): void {
            const ports = (root.ui.runtime.features || {}).listenerPortCandidate;
            if (root.ui.uiVisible && ports && !portDialog.active && root.portCandidate.token !== ports.token) {
                root.portCandidate = ports;
                root.portEpoch = root.ui.runtime.epoch || "";
                root.portGeneration = root.ui.runtime.selectionGeneration || "0";
                portDialog.active = true;
                const dialog = portDialog.item as ThemedDialog;
                if (dialog)
                    dialog.open();
            }
        }
    }
    Component.onDestruction: releasePreviews()
    SettingsGroup {
        theme: root.theme
        title: qsTranslate("settings_page", "Proxy integration")
        Layout.fillWidth: true
        Repeater {
            model: [
                {
                    kind: "system_proxy",
                    label: qsTranslate("settings_page", "System proxy")
                },
                {
                    kind: "tun",
                    label: qsTranslate("settings_page", "Virtual network adapter (TUN)")
                }
            ]
            delegate: SettingsRow {
                id: integrationRow
                required property var modelData
                theme: root.theme
                title: modelData.label
                Layout.fillWidth: true
                inlineControl: true
                controlWidth: 60
                description: qsTranslate("settings_page", "Requested: %1 · Actual: %2").arg((root.ui.runtime.preferences || {})[modelData.kind === "tun" ? "tunDesired" : "systemProxyDesired"] ? qsTranslate("settings_page", "On") : qsTranslate("settings_page", "Off")).arg(modelData.kind === "tun" ? root.tunStatus(root.ui.runtime.tunState || "Off") : root.ui.runtime.systemProxy ? qsTranslate("settings_page", "On") : qsTranslate("settings_page", "Off")) + ((!root.ui.runtime.selectedId || root.ui.runtime.readOnly || (root.ui.runtime.capabilities || {})[modelData.kind === "tun" ? "tun" : "systemProxy"] === "Unsupported") ? " · " + qsTranslate("settings_page", "Unavailable: check the profile, permissions and platform support.") : "")
                ThemedSwitch {
                    theme: root.theme
                    width: parent.width
                    implicitHeight: Math.max(48, root.theme.controlHeight)
                    objectName: integrationRow.modelData.kind === "tun" ? "settings_tun" : "settings_system_proxy"
                    Accessible.name: integrationRow.modelData.label
                    checked: integrationRow.modelData.kind === "tun" ? root.ui.runtime.tunState === "On" : !!root.ui.runtime.systemProxy
                    enabled: root.ui.settingsReady && !!root.ui.runtime.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly && (root.ui.runtime.capabilities || {})[integrationRow.modelData.kind === "tun" ? "tun" : "systemProxy"] !== "Unsupported"
                    onClicked: root.ui.runtimeCommand(integrationRow.modelData.kind, [checked ? "true" : "false"])
                }
            }
        }
        Label {
            text: qsTranslate("settings_page", "TUN requires the Windows service and may request administrator authorization.")
            color: root.theme.muted
            font.pixelSize: root.theme.supportingSize
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.topMargin: 8
        }
        SettingsAction {
            objectName: "settings_tun_help_toggle"
            theme: root.theme
            text: root.tunHelpExpanded ? qsTranslate("settings_page", "Hide TUN authorization details") : qsTranslate("settings_page", "TUN authorization details")
            onClicked: root.tunHelpExpanded = !root.tunHelpExpanded
        }
        Label {
            objectName: "settings_tun_help_details"
            visible: root.tunHelpExpanded
            text: qsTranslate("settings_page", "TUN needs the Windows service. Enabling TUN can ask for one administrator confirmation to install it or authorize this folder; moving or renaming the folder requires authorization again. QuickClash itself keeps running as an ordinary user.")
            color: root.theme.muted
            font.pixelSize: root.theme.supportingSize
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: !!(root.ui.runtime.features || {}).serviceSetupPending || !!root.ui.runtime.tunPending
            text: qsTranslate("settings_page", "Waiting for Windows administrator confirmation…")
            wrapMode: Text.WordWrap
            color: root.theme.muted
            Layout.fillWidth: true
        }
        Label {
            objectName: "service_setup_log"
            visible: text.length > 0
            text: (root.ui.runtime.features || {}).serviceSetupLog || ""
            color: root.theme.muted
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
        }
        Label {
            visible: !!root.ui.runtime.tunDevice
            text: qsTranslate("settings_page", "TUN adapter: %1").arg(root.ui.runtime.tunDevice || "")
            color: root.theme.muted
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
        }
        SettingsAction {
            theme: root.theme
            text: qsTranslate("settings_page", "Reapply proxy using current system settings")
            Layout.maximumWidth: parent.width
            visible: !!root.ui.runtime.proxyConflict
            enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy
            onClicked: root.ui.runtimeCommand("reapply_system_proxy")
        }
    }
    SettingsGroup {
        theme: root.theme
        title: qsTranslate("settings_page", "Runtime configuration")
        Layout.fillWidth: true
        SettingsRow {
            theme: root.theme
            title: qsTranslate("settings_page", "Routing mode")
            Layout.fillWidth: true
            ThemedComboBox {
                theme: root.theme
                objectName: "settings_outbound_mode"
                Accessible.name: qsTranslate("settings_page", "Routing mode")
                width: parent.width
                model: [qsTranslate("settings_page", "Rule"), qsTranslate("settings_page", "Global"), qsTranslate("settings_page", "Direct")]
                currentIndex: Math.max(0, ["rule", "global", "direct"].indexOf(root.draft.mode))
                enabled: root.editable
                onActivated: root.ui.editSetting("mode", ["rule", "global", "direct"][currentIndex])
            }
        }
        Repeater {
            model: [
                {
                    key: "mixedPort",
                    name: "mixed_port",
                    label: qsTranslate("settings_page", "Proxy port")
                },
                {
                    key: "controllerPort",
                    name: "controller_port",
                    label: qsTranslate("settings_page", "Controller port")
                }
            ]
            delegate: SettingsRow {
                id: portRow
                required property var modelData
                theme: root.theme
                title: modelData.label
                description: qsTranslate("settings_page", "1024–65535; the two ports must differ.")
                stackedControl: true
                Layout.fillWidth: true
                ThemedField {
                    theme: root.theme
                    objectName: portRow.modelData.name
                    Accessible.name: portRow.modelData.label
                    width: parent.width
                    text: root.draft[portRow.modelData.key] || ""
                    maximumLength: 5
                    inputMethodHints: Qt.ImhDigitsOnly
                    enabled: root.editable
                    onTextEdited: root.ui.editSetting(portRow.modelData.key, text)
                }
            }
        }
        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 8
            SettingsAction {
                theme: root.theme
                objectName: "suggest_listener_ports"
                width: Math.min(implicitWidth, parent.width)
                text: qsTranslate("settings_page", "Use available ports…")
                enabled: root.editable && !root.session.networkStale
                onClicked: root.ui.runtimeCommand("suggest_listener_ports")
            }
            SettingsAction {
                theme: root.theme
                width: Math.min(implicitWidth, parent.width)
                text: qsTranslate("settings_page", "Retry")
                visible: !!root.ui.runtime.listenerRecoveryBlocked && !root.ui.runtime.ready
                enabled: root.editable
                onClicked: root.ui.runtimeCommand("start")
            }
        }
        SettingsRow {
            theme: root.theme
            title: qsTranslate("settings_page", "DNS mode")
            Layout.fillWidth: true
            ThemedComboBox {
                theme: root.theme
                objectName: "settings_dns_mode"
                Accessible.name: qsTranslate("settings_page", "DNS mode")
                width: parent.width
                model: [qsTranslate("settings_page", "Use profile DNS"), "fake-ip", "redir-host"]
                currentIndex: Math.max(0, ["profile", "fake-ip", "redir-host"].indexOf(root.draft.dnsMode))
                enabled: root.editable
                onActivated: root.ui.editSetting("dnsMode", ["profile", "fake-ip", "redir-host"][currentIndex])
            }
        }
        SettingsAction {
            theme: root.theme
            text: qsTranslate("settings_page", "Clear DNS cache")
            Layout.maximumWidth: parent.width
            Layout.topMargin: 8
            enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy && (root.ui.runtime.features || {}).dnsResult !== "Clearing"
            onClicked: root.ui.runtimeCommand("flush_dns")
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: root.theme.muted
            text: ({
                    Clearing: qsTranslate("settings_page", "Clearing DNS cache…"),
                    Cleared: qsTranslate("settings_page", "DNS cache cleared"),
                    Failed: qsTranslate("settings_page", "DNS cache could not be cleared")
                })[(root.ui.runtime.features || {}).dnsResult] || ""
            visible: text.length > 0
        }
        Label {
            objectName: "network_settings_state"
            Layout.fillWidth: true
            Layout.topMargin: 12
            wrapMode: Text.WordWrap
            color: root.theme.muted
            text: root.settingsState.pending ? qsTranslate("settings_page", "Applying requested settings: %1").arg(root.networkSummary(root.settingsState.requested || {})) : !root.settingsState.running ? qsTranslate("settings_page", "Core stopped · saved settings are not active") : root.showComparison ? qsTranslate("settings_page", "Saved and active settings differ.") : qsTranslate("settings_page", "Saved network settings are active.")
        }
        Label {
            objectName: "network_settings_comparison"
            Layout.fillWidth: true
            visible: root.showComparison
            wrapMode: Text.WordWrap
            color: root.theme.muted
            text: qsTranslate("settings_page", "Saved: %1").arg(root.networkSummary(root.settingsState.saved || {})) + "\n" + (root.settingsState.running ? qsTranslate("settings_page", "Active: %1").arg(root.networkSummary(root.settingsState.effective || {})) : qsTranslate("settings_page", "Active: core stopped"))
        }
        Label {
            Layout.fillWidth: true
            visible: root.session.networkStale
            wrapMode: Text.WordWrap
            color: root.theme.warning
            text: qsTranslate("settings_page", "The configuration or saved values changed. Reload, or keep your input and review against the current values.")
        }
        Flow {
            visible: root.session.networkStale
            Layout.fillWidth: true
            spacing: 8
            SettingsAction {
                theme: root.theme
                width: Math.min(implicitWidth, parent.width)
                text: qsTranslate("settings_page", "Reload saved values")
                onClicked: root.ui.resetSettingsDraft("network")
            }
            SettingsAction {
                theme: root.theme
                width: Math.min(implicitWidth, parent.width)
                text: qsTranslate("settings_page", "Keep input and review again")
                onClicked: root.ui.resetSettingsDraft("network", true)
            }
        }
        Label {
            objectName: "settings_network_feedback"
            Layout.fillWidth: true
            Layout.topMargin: 8
            wrapMode: Text.WordWrap
            color: !root.session.networkValid || root.session.networkResult === "Failed" || root.confirmationError ? root.theme.error : root.theme.muted
            text: root.confirmationError || (root.session.networkPending ? qsTranslate("settings_page", "Applying network changes…") : root.session.networkDirty && !root.session.networkValid ? qsTranslate("settings_page", "Enter two different ports from 1024 to 65535.") : root.session.networkResult === "Failed" ? qsTranslate("settings_page", "Could not apply settings. Your input has been kept.") : root.session.networkResult === "Saved" ? qsTranslate("settings_page", "Network settings saved.") : root.session.networkDirty ? qsTranslate("settings_page", "You have unapplied network changes.") : qsTranslate("settings_page", "No network changes."))
        }
        Flow {
            objectName: "settings_network_actions"
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 8
            SettingsAction {
                theme: root.theme
                objectName: "discard_network_settings"
                width: Math.min(implicitWidth, parent.width)
                text: qsTranslate("settings_page", "Discard changes")
                enabled: root.session.networkDirty && !root.session.networkPending
                onClicked: root.ui.resetSettingsDraft("network")
            }
            SettingsAction {
                theme: root.theme
                objectName: "apply_network_settings"
                width: Math.min(implicitWidth, parent.width)
                text: qsTranslate("settings_page", "Apply network settings…")
                prominent: true
                enabled: root.editable && root.session.networkDirty && root.session.networkValid && !root.session.networkStale
                onClicked: root.requestNetworkSettings()
            }
        }
    }
    SettingsGroup {
        theme: root.theme
        title: qsTranslate("settings_page", "Node delay testing")
        Layout.fillWidth: true
        Label {
            text: qsTranslate("settings_page", "This HTTPS target is used by individual and group node delay tests. Tests run only when requested.")
            wrapMode: Text.WordWrap
            color: root.theme.muted
            Layout.fillWidth: true
        }
        Label {
            text: qsTranslate("settings_page", "HTTPS target for node delay")
            color: root.theme.text
            Layout.fillWidth: true
            Layout.topMargin: 12
            wrapMode: Text.WordWrap
        }
        ThemedField {
            theme: root.theme
            objectName: "delay_target"
            Accessible.name: qsTranslate("settings_page", "HTTPS target for node delay")
            Layout.fillWidth: true
            Layout.topMargin: 8
            text: root.session.target || ""
            maximumLength: 8192
            enabled: root.ui.settingsReady && !root.ui.runtime.readOnly && !root.session.targetPending
            onTextEdited: root.ui.editSetting("target", text)
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            wrapMode: Text.WordWrap
            color: root.session.targetValid && !root.session.targetStale && root.session.targetResult !== "Failed" ? root.theme.muted : root.theme.error
            text: root.session.targetStale ? qsTranslate("settings_page", "The saved target changed. Reload, or keep your input and review again.") : root.session.targetPending ? qsTranslate("settings_page", "Saving target…") : !root.session.targetValid ? qsTranslate("settings_page", "Use an HTTPS URL without credentials or a fragment.") : root.session.targetResult === "Failed" ? qsTranslate("settings_page", "Could not save the target. Your input has been kept.") : root.session.targetResult === "Saved" ? qsTranslate("settings_page", "Delay target saved.") : qsTranslate("settings_page", "Saved separately from network settings.")
        }
        Flow {
            visible: root.session.targetStale
            Layout.fillWidth: true
            spacing: 8
            SettingsAction {
                theme: root.theme
                width: Math.min(implicitWidth, parent.width)
                text: qsTranslate("settings_page", "Reload saved values")
                onClicked: root.ui.resetSettingsDraft("target")
            }
            SettingsAction {
                theme: root.theme
                width: Math.min(implicitWidth, parent.width)
                text: qsTranslate("settings_page", "Keep input and review again")
                onClicked: root.ui.resetSettingsDraft("target", true)
            }
        }
        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 8
            SettingsAction {
                theme: root.theme
                width: Math.min(implicitWidth, parent.width)
                text: qsTranslate("settings_page", "Discard changes")
                enabled: root.session.targetDirty && !root.session.targetPending
                onClicked: root.ui.resetSettingsDraft("target")
            }
            SettingsAction {
                theme: root.theme
                objectName: "save_delay_target"
                width: Math.min(implicitWidth, parent.width)
                text: qsTranslate("settings_page", "Save delay test target")
                enabled: root.ui.settingsReady && !root.ui.runtime.readOnly && root.session.targetDirty && root.session.targetValid && !root.session.targetStale && !root.session.targetPending && ((root.ui.runtime.features || {}).delayTest || {}).state !== "Running"
                onClicked: root.ui.submitDelayTarget()
            }
        }
    }

    Loader {
        id: networkDialog
        objectName: "settings_loader_networkDialog"
        active: false
        sourceComponent: ThemedDialog {
            id: networkConfirmation
            objectName: "network_settings_confirmation"
            theme: root.theme
            parent: Overlay.overlay
            anchors.centerIn: parent
            width: Math.min(560, parent.width - 32)
            height: Math.min(360, parent.height - 32)
            implicitHeight: height
            title: qsTranslate("settings_page", "Apply network settings?")
            modal: true
            standardButtons: Dialog.Ok | Dialog.Cancel
            contentItem: ThemedScrollView {
                id: networkConfirmationBody
                theme: root.theme
                form: networkConfirmationBodyForm
                Label {
                    id: networkConfirmationBodyForm
                    wrapMode: Text.WordWrap
                    color: root.theme.text
                    text: qsTranslate("settings_page", "Previous saved values: %1").arg(root.networkSummary(root.ui.settingsSession.baseline || {})) + "\n" + qsTranslate("settings_page", "Configuration: %1").arg(root.ui.runtime.selectedName || qsTranslate("settings_page", "No profile selected")) + "\n" + root.networkSummary({
                        mode: root.pendingNetworkArgs[0],
                        mixedPort: root.pendingNetworkArgs[1],
                        controllerPort: root.pendingNetworkArgs[2],
                        dnsMode: root.pendingNetworkArgs[3]
                    }) + "\n" + (root.ui.runtime.ready ? qsTranslate("settings_page", "Supported changes reload the running core. Changing the controller port restarts it. Active connections may be interrupted; failure restores the confirmed settings.") : qsTranslate("settings_page", "These settings are saved for the next core session. They are not active while the core is stopped."))
                }
            }
            onAccepted: {
                if (!root.ui.submitNetworkSettings(root.pendingNetworkArgs))
                    root.confirmationError = qsTranslate("settings_page", "Settings changed while confirming. Review your input and apply again.");
            }
            onRejected: root.ui.runtimeCommand("cancel_listener_ports")
            onClosed: {
                root.pendingNetworkArgs = [];
                networkDialog.active = false;
            }
        }
    }
    Loader {
        id: portDialog
        objectName: "settings_loader_portDialog"
        active: false
        sourceComponent: ThemedDialog {
            theme: root.theme
            parent: Overlay.overlay
            anchors.centerIn: parent
            width: Math.min(480, parent.width - 32)
            height: Math.min(360, parent.height - 32)
            implicitHeight: height
            objectName: "settings_port_confirmation"
            title: qsTranslate("settings_page", "Fill available ports into the draft?")
            standardButtons: Dialog.Ok | Dialog.Cancel
            modal: true
            contentItem: ThemedScrollView {
                theme: root.theme
                form: portBody
                Label {
                    id: portBody
                    text: qsTranslate("settings_page", "Proxy port: %1 · Controller port: %2. This only fills the draft. Review and apply network settings separately; ports are checked again when applying.").arg(root.portCandidate.mixedPort || "").arg(root.portCandidate.controllerPort || "")
                    wrapMode: Text.WordWrap
                    color: root.theme.text
                }
            }
            onAccepted: {
                if (!root.ui.useSuggestedPorts(root.portCandidate.token || "", root.portEpoch, root.portGeneration))
                    root.confirmationError = qsTranslate("settings_page", "The port suggestion expired. Request another suggestion.");
            }
            onRejected: root.ui.runtimeCommand("cancel_listener_ports")
            onClosed: portDialog.active = false
        }
    }
}
