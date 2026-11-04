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
    objectName: "settings_general"
    function startupStatus(value): string {
        const labels = {
            On: qsTranslate("settings_page", "Enabled"),
            Off: qsTranslate("settings_page", "Disabled"),
            PathMismatch: qsTranslate("settings_page", "Application moved; enable again to repair"),
            DisabledBySystem: qsTranslate("settings_page", "Disabled in Windows startup settings"),
            Unsupported: qsTranslate("settings_page", "Unavailable on this platform")
        };
        return labels[value] || qsTranslate("settings_page", "Unable to read startup status");
    }
    SettingsGroup {
        theme: root.theme
        title: qsTranslate("settings_page", "Appearance and language")
        Layout.fillWidth: true
        SettingsRow {
            theme: root.theme
            title: qsTranslate("settings_page", "Theme")
            Layout.fillWidth: true
            controlWidth: root.theme.scaledSize(300)
            Flow {
                width: parent.width
                spacing: 4
                Repeater {
                    model: [
                        {
                            key: "system",
                            label: qsTranslate("settings_page", "Follow system")
                        },
                        {
                            key: "light",
                            label: qsTranslate("settings_page", "Light")
                        },
                        {
                            key: "dark",
                            label: qsTranslate("settings_page", "Dark")
                        }
                    ]
                    delegate: SettingsAction {
                        required property var modelData
                        objectName: "appearance_" + modelData.key
                        theme: root.theme
                        width: Math.min(implicitWidth, parent.width)
                        text: modelData.label
                        selected: root.ui.themeMode === modelData.key
                        enabled: root.ui.settingsReady && !root.ui.runtime.readOnly
                        onClicked: root.ui.themeMode = modelData.key
                    }
                }
            }
        }
        SettingsRow {
            theme: root.theme
            title: qsTranslate("settings_page", "Language")
            objectName: "appearance_language_heading"
            description: qsTranslate("settings_page", "Changes apply immediately.")
            Layout.fillWidth: true
            ThemedComboBox {
                theme: root.theme
                objectName: "settings_language"
                Accessible.name: qsTranslate("settings_page", "Language")
                width: parent.width
                model: [qsTranslate("settings_page", "Follow system"), "English", "简体中文"]
                currentIndex: Math.max(0, ["system", "en", "zh"].indexOf(root.ui.languageMode))
                enabled: root.ui.settingsReady && !root.ui.runtime.readOnly
                onActivated: root.ui.languageMode = ["system", "en", "zh"][currentIndex]
            }
        }
    }
    SettingsGroup {
        theme: root.theme
        title: qsTranslate("settings_page", "Startup")
        Layout.fillWidth: true
        SettingsRow {
            theme: root.theme
            title: qsTranslate("settings_page", "Start QuickClash when I sign in")
            description: qsTranslate("settings_page", "Registration: %1. Startup restores your saved network intent after the engine is ready.").arg(root.startupStatus((root.ui.runtime.startup || {}).state))
            inlineControl: true
            controlWidth: 60
            Layout.fillWidth: true
            ThemedSwitch {
                theme: root.theme
                objectName: "settings_startup_enabled"
                Accessible.name: qsTranslate("settings_page", "Start QuickClash when I sign in")
                width: parent.width
                implicitHeight: Math.max(48, root.theme.controlHeight)
                checked: !!(root.ui.runtime.startup || {}).requested
                enabled: root.ui.settingsReady && !!(root.ui.runtime.startup || {}).supported && !root.ui.runtime.readOnly
                onClicked: root.ui.runtimeCommand("startup_enabled", [checked ? "true" : "false"])
            }
        }
        ThemedCheckBox {
            theme: root.theme
            Layout.fillWidth: true
            text: qsTranslate("settings_page", "Hide to the tray at sign-in")
            checked: (root.ui.runtime.startup || {}).hidden !== false
            enabled: root.ui.settingsReady && !!(root.ui.runtime.startup || {}).supported && !!(root.ui.runtime.startup || {}).requested && !root.ui.runtime.readOnly
            onClicked: root.ui.runtimeCommand("startup_hidden", [checked ? "true" : "false"])
        }
        SettingsAction {
            theme: root.theme
            text: qsTranslate("settings_page", "Refresh startup status")
            Layout.maximumWidth: parent.width
            Layout.topMargin: 8
            enabled: root.ui.settingsReady
            onClicked: root.ui.runtimeCommand("startup_refresh")
        }
    }
    SettingsGroup {
        theme: root.theme
        title: qsTranslate("settings_page", "Window and tray")
        Layout.fillWidth: true
        SettingsRow {
            theme: root.theme
            title: qsTranslate("settings_page", "Unload interface when closed to tray (experimental)")
            description: qsTranslate("settings_page", "Off by default. Minimizing keeps the interface and taskbar window. Closing to an available tray unloads the interface; the network engine and subscription updates keep running. Reopening from the tray rebuilds the interface.")
            inlineControl: true
            controlWidth: 60
            Layout.fillWidth: true
            ThemedSwitch {
                theme: root.theme
                objectName: "settings_unload_interface"
                Accessible.name: qsTranslate("settings_page", "Unload interface when closed to tray (experimental)")
                width: parent.width
                implicitHeight: Math.max(48, root.theme.controlHeight)
                checked: root.ui.unloadWhenHidden
                enabled: root.ui.settingsReady && !root.ui.runtime.readOnly && (root.ui.trayAvailable || checked)
                onClicked: root.ui.unloadWhenHidden = checked
            }
        }
        Label {
            text: root.ui.trayAvailable ? qsTranslate("settings_page", "Closing hides the window to the tray. Use Quit to stop QuickClash.") : qsTranslate("settings_page", "No tray is available. Closing offers a recoverable window or Quit.")
            font.pixelSize: root.theme.supportingSize
            color: root.theme.muted
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.topMargin: 8
        }
    }
}
