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
    objectName: "settings_resources"
    readonly property var coreUpdate: ui.runtime.coreUpdate || ({})
    readonly property var features: ui.runtime.features || ({})
    property string pendingCoreCommand: ""
    property bool coreAdvanced: false
    property bool geoAdvanced: false
    function openDialog(loader): void {
        loader.active = true;
        loader.item.open();
    }
    function closeDialogs(): void {
        for (const loader of [coreDialog, geoDownloadDialog, geoRollbackDialog, geoPendingDialog, geoBundledDialog])
            loader.active = false;
        pendingCoreCommand = "";
    }
    Connections {
        target: root.ui
        function onUiAboutToHide(): void {
            root.closeDialogs();
        }
    }
    FlatTile {
        theme: root.theme
        Layout.fillWidth: true
        implicitHeight: coreContent.implicitHeight + 32
        ColumnLayout {
            id: coreContent
            anchors.fill: parent
            anchors.margins: root.theme.panelPadding
            spacing: 8
            Label {
                text: qsTranslate("settings_page", "Core updates")
                color: root.theme.text
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: root.theme.muted
                text: qsTranslate("settings_page", "Running: %1 · Selected: %2 · Compatible catalog: %3").arg(root.ui.runtime.coreVersion || "—").arg((root.ui.runtime.preferences || {}).coreArtifactId || qsTranslate("settings_page", "Bundled")).arg(root.coreUpdate.version || "—")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: root.theme.muted
                text: qsTranslate("settings_page", "Only reviewed official assets in this client catalog can be downloaded. New upstream versions require a compatible catalog. Downloading does not switch the running core.")
            }
            SettingsRow {
                theme: root.theme
                title: qsTranslate("settings_page", "Core download path")
                Layout.fillWidth: true
                ThemedComboBox {
                    theme: root.theme
                    Accessible.name: qsTranslate("settings_page", "Core download path")
                    width: parent.width
                    model: [qsTranslate("settings_page", "Direct download"), qsTranslate("settings_page", "Use current proxy")]
                    currentIndex: root.coreUpdate.route === "CurrentProxy" ? 1 : 0
                    enabled: root.ui.settingsReady && !root.ui.runtime.busy && !root.ui.runtime.readOnly
                    onActivated: root.ui.runtimeCommand("core_route", [currentIndex === 1 ? "CurrentProxy" : "Direct"])
                }
            }
            SettingsAction {
                objectName: "settings_core_advanced"
                theme: root.theme
                text: qsTranslate("settings_page", "Advanced recovery")
                selected: root.coreAdvanced
                checkable: true
                checked: root.coreAdvanced
                Accessible.checkable: true
                Accessible.checked: root.coreAdvanced
                onClicked: root.coreAdvanced = checked
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: [
                        {
                            command: "core_check",
                            label: qsTranslate("settings_page", "Check compatible core"),
                            confirm: false
                        },
                        {
                            command: "core_download",
                            label: qsTranslate("settings_page", "Download or repair core"),
                            confirm: false
                        },
                        {
                            command: "core_apply",
                            label: qsTranslate("settings_page", "Apply downloaded core…"),
                            confirm: true
                        },
                        {
                            command: "core_rollback",
                            label: qsTranslate("settings_page", "Roll back core…"),
                            confirm: true
                        },
                        {
                            command: "core_bundled",
                            label: qsTranslate("settings_page", "Use bundled core…"),
                            confirm: true
                        }
                    ]
                    delegate: SettingsAction {
                        objectName: "settings_" + modelData.command
                        width: Math.min(implicitWidth, parent.width)
                        required property var modelData
                        theme: root.theme
                        text: modelData.label
                        visible: !["core_rollback", "core_bundled"].includes(modelData.command) || root.coreAdvanced
                        enabled: root.ui.settingsReady && !root.ui.runtime.busy && !root.ui.runtime.readOnly && (modelData.command !== "core_apply" || root.coreUpdate.state === "Verified") && (modelData.command !== "core_rollback" || (root.ui.runtime.preferences || {}).previousCoreArtifactId !== undefined) && (!modelData.confirm || !!root.ui.runtime.selectedId)
                        onClicked: {
                            if (modelData.confirm) {
                                root.pendingCoreCommand = modelData.command;
                                root.openDialog(coreDialog);
                            } else
                                root.ui.runtimeCommand(modelData.command);
                        }
                    }
                }
                SettingsAction {
                    width: Math.min(implicitWidth, parent.width)
                    theme: root.theme
                    text: qsTranslate("settings_page", "Cancel core download")
                    visible: ["Downloading", "Extracting"].indexOf(root.coreUpdate.state || "") >= 0
                    onClicked: root.ui.runtimeCommand("core_cancel")
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: root.theme.muted
                text: ({
                        Available: qsTranslate("settings_page", "Compatible catalog checked"),
                        Downloading: qsTranslate("settings_page", "Downloading core…"),
                        Extracting: qsTranslate("settings_page", "Verifying and extracting core…"),
                        Verified: qsTranslate("settings_page", "Core verified; ready to apply"),
                        Applying: qsTranslate("settings_page", "Applying core…"),
                        Applied: qsTranslate("settings_page", "Core selection saved"),
                        Failed: qsTranslate("settings_page", "Core update failed; confirmed selection kept"),
                        Cancelled: qsTranslate("settings_page", "Core download cancelled")
                    })[root.coreUpdate.state] || ""
            }
        }
    }
    SettingsGroup {
        theme: root.theme
        title: qsTranslate("settings_page", "Geo databases")
        Layout.fillWidth: true
        Label {
            text: qsTranslate("settings_page", "Geo snapshot: %1 · Last successful download: %2").arg((((root.ui.runtime.geoUpdate || {}).snapshot || {})["geoip.dat"] || {}).revision || qsTranslate("settings_page", "Bundled")).arg((((root.ui.runtime.geoUpdate || {}).settings || {}).lastSuccess || "—"))
            Layout.fillWidth: true
            wrapMode: Text.WrapAnywhere
            color: root.theme.muted
        }
        SettingsRow {
            theme: root.theme
            title: qsTranslate("settings_page", "Geo download path")
            Layout.fillWidth: true
            ThemedComboBox {
                id: geoRoute
                width: parent.width
                theme: root.theme
                Accessible.name: qsTranslate("settings_page", "Geo download path")
                model: [qsTranslate("settings_page", "Direct"), qsTranslate("settings_page", "Current proxy")]
                currentIndex: (((root.ui.runtime.geoUpdate || {}).settings || {}).route || "Direct") === "CurrentProxy" ? 1 : 0
                enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
                onActivated: root.ui.runtimeCommand("geo_settings", [currentIndex === 1 ? "CurrentProxy" : "Direct", String((((root.ui.runtime.geoUpdate || {}).settings || {}).intervalHours || 0))])
            }
        }
        SettingsRow {
            theme: root.theme
            title: qsTranslate("settings_page", "Geo download schedule")
            description: qsTranslate("settings_page", "Scheduled downloads do not apply the downloaded snapshot automatically.")
            Layout.fillWidth: true
            ThemedComboBox {
                width: parent.width
                theme: root.theme
                Accessible.name: qsTranslate("settings_page", "Geo download schedule")
                model: [qsTranslate("settings_page", "Manual Geo updates"), qsTranslate("settings_page", "Download Geo daily"), qsTranslate("settings_page", "Download Geo weekly")]
                currentIndex: [0, 24, 168].indexOf((((root.ui.runtime.geoUpdate || {}).settings || {}).intervalHours || 0))
                enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
                onActivated: root.ui.runtimeCommand("geo_settings", [(((root.ui.runtime.geoUpdate || {}).settings || {}).route || "Direct"), String([0, 24, 168][currentIndex])])
            }
        }
        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 8
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Update Geo databases…")
                enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly
                onClicked: root.openDialog(geoDownloadDialog)
            }
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Apply downloaded Geo…")
                visible: !!(root.ui.runtime.geoUpdate || {}).pending
                enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly
                onClicked: root.openDialog(geoPendingDialog)
            }
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Cancel Geo update")
                visible: ["Downloading", "Validating"].indexOf((root.ui.runtime.features || {}).geoResult || "") >= 0
                onClicked: root.ui.runtimeCommand("cancel_geo")
            }
        }
        SettingsAction {
            objectName: "settings_geo_advanced"
            theme: root.theme
            text: qsTranslate("settings_page", "Advanced recovery")
            selected: root.geoAdvanced
            checkable: true
            checked: root.geoAdvanced
            Accessible.checkable: true
            Accessible.checked: root.geoAdvanced
            Layout.topMargin: 8
            onClicked: root.geoAdvanced = checked
        }
        Flow {
            objectName: "settings_geo_recovery_actions"
            visible: root.geoAdvanced
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 8
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Roll back Geo databases…")
                enabled: !!root.ui.runtime.selectedId && (root.ui.runtime.preferences || {}).previousGeoSnapshot !== undefined && !root.ui.runtime.busy && !root.ui.runtime.readOnly
                onClicked: root.openDialog(geoRollbackDialog)
            }
            SettingsAction {
                width: Math.min(implicitWidth, parent.width)
                theme: root.theme
                text: qsTranslate("settings_page", "Use bundled Geo databases…")
                enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly
                onClicked: root.openDialog(geoBundledDialog)
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            wrapMode: Text.WordWrap
            color: root.theme.muted
            text: ({
                    Pending: qsTranslate("settings_page", "Geo snapshot downloaded; apply when ready"),
                    Downloading: qsTranslate("settings_page", "Downloading both Geo databases…"),
                    Validating: qsTranslate("settings_page", "Validating Geo databases…"),
                    Applying: qsTranslate("settings_page", "Applying Geo databases…"),
                    Applied: qsTranslate("settings_page", "Geo snapshot saved"),
                    Unchanged: qsTranslate("settings_page", "Geo databases are unchanged"),
                    Failed: qsTranslate("settings_page", "Geo update failed; previous snapshot kept"),
                    Cancelled: qsTranslate("settings_page", "Geo update cancelled")
                })[root.features.geoResult] || ""
        }
    }

    Loader {
        id: coreDialog
        objectName: "settings_loader_coreDialog"
        active: false
        sourceComponent: ThemedDialog {
            id: coreConfirmation
            objectName: "core_update_confirmation"
            theme: root.theme
            parent: Overlay.overlay
            anchors.centerIn: parent
            width: Math.min(560, parent.width - 32)
            height: Math.min(320, parent.height - 32)
            implicitHeight: height
            title: qsTranslate("settings_page", "Switch the managed core?")
            modal: true
            standardButtons: Dialog.Ok | Dialog.Cancel
            contentItem: ThemedScrollView {
                id: coreConfirmationBody
                theme: root.theme
                form: coreConfirmationBodyForm
                Label {
                    id: coreConfirmationBodyForm
                    wrapMode: Text.WordWrap
                    color: root.theme.text
                    text: qsTranslate("settings_page", "The selected configuration and Geo files are validated first. A running core restarts and existing connections are interrupted. Failure restores the confirmed core and configuration.")
                }
            }
            onAccepted: root.ui.runtimeCommand(root.pendingCoreCommand, ["confirmed"])
            onClosed: coreDialog.active = false
        }
    }

    Loader {
        id: geoDownloadDialog
        objectName: "settings_loader_geoDownloadDialog"
        active: false
        sourceComponent: ThemedDialog {
            id: geoConfirmation
            objectName: "geoDownloadDialog"
            parent: Overlay.overlay
            width: Math.min(560, parent.width - 32)
            height: Math.min(360, parent.height - 32)
            implicitHeight: height
            theme: root.theme
            title: qsTranslate("settings_page", "Download and apply Geo databases?")
            modal: true
            anchors.centerIn: parent
            standardButtons: Dialog.Ok | Dialog.Cancel
            contentItem: ThemedScrollView {
                theme: root.theme
                form: geoBody1
                Label {
                    id: geoBody1
                    wrapMode: Text.WordWrap
                    color: root.theme.text
                    text: qsTranslate("settings_page", "Downloads GeoIP and GeoSite from MetaCubeX over HTTPS, validates both with Clash Meta (mihomo), and applies them together in both normal and TUN modes. Existing connections restart. The previous snapshot is retained.")
                }
            }
            onAccepted: root.ui.runtimeCommand("update_geo")
            onClosed: geoDownloadDialog.active = false
        }
    }

    Loader {
        id: geoRollbackDialog
        objectName: "settings_loader_geoRollbackDialog"
        active: false
        sourceComponent: ThemedDialog {
            id: geoRollback
            objectName: "geoRollbackDialog"
            parent: Overlay.overlay
            width: Math.min(560, parent.width - 32)
            height: Math.min(360, parent.height - 32)
            implicitHeight: height
            theme: root.theme
            title: qsTranslate("settings_page", "Restore the previous Geo snapshot?")
            modal: true
            anchors.centerIn: parent
            standardButtons: Dialog.Ok | Dialog.Cancel
            onAccepted: root.ui.runtimeCommand("rollback_geo", ["confirmed"])
            onClosed: geoRollbackDialog.active = false
        }
    }

    Loader {
        id: geoPendingDialog
        objectName: "settings_loader_geoPendingDialog"
        active: false
        sourceComponent: ThemedDialog {
            id: geoPendingConfirmation
            objectName: "geoPendingDialog"
            parent: Overlay.overlay
            width: Math.min(560, parent.width - 32)
            height: Math.min(360, parent.height - 32)
            implicitHeight: height
            theme: root.theme
            title: qsTranslate("settings_page", "Apply downloaded Geo snapshot?")
            modal: true
            anchors.centerIn: parent
            standardButtons: Dialog.Ok | Dialog.Cancel
            contentItem: ThemedScrollView {
                theme: root.theme
                form: geoBody2
                Label {
                    id: geoBody2
                    wrapMode: Text.WordWrap
                    color: root.theme.text
                    text: qsTranslate("settings_page", "Both databases are validated again. Applying restarts the engine and interrupts active connections.")
                }
            }
            onAccepted: root.ui.runtimeCommand("apply_pending_geo", ["confirmed"])
            onClosed: geoPendingDialog.active = false
        }
    }

    Loader {
        id: geoBundledDialog
        objectName: "settings_loader_geoBundledDialog"
        active: false
        sourceComponent: ThemedDialog {
            id: geoBundled
            objectName: "geoBundledDialog"
            parent: Overlay.overlay
            width: Math.min(560, parent.width - 32)
            height: Math.min(360, parent.height - 32)
            implicitHeight: height
            theme: root.theme
            title: qsTranslate("settings_page", "Use the bundled Geo databases?")
            modal: true
            anchors.centerIn: parent
            standardButtons: Dialog.Ok | Dialog.Cancel
            contentItem: ThemedScrollView {
                theme: root.theme
                form: geoBody3
                Label {
                    id: geoBody3
                    wrapMode: Text.WordWrap
                    color: root.theme.text
                    text: qsTranslate("settings_page", "Validates the offline databases from the application package and applies them together. The current snapshot remains available for rollback.")
                }
            }
            onAccepted: root.ui.runtimeCommand("bundled_geo", ["confirmed"])
            onClosed: geoBundledDialog.active = false
        }
    }
}
