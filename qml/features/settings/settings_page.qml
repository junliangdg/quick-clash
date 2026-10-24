pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ScrollView {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    contentWidth: availableWidth
    clip: true
    ColumnLayout {
        width: root.availableWidth
        spacing: 14
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: moduleContent.implicitHeight + 40
            ColumnLayout {
                id: moduleContent
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                Label {
                    text: qsTr("More pages")
                    color: root.theme.text
                    font.bold: true
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: root.ui.navigation.filter(function (entry) {
                            return ["home", "proxies", "settings"].indexOf(entry.pageId) < 0;
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
            implicitHeight: repair.implicitHeight + 40
            ColumnLayout {
                id: repair
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                Label {
                    text: qsTr("Core and network repair")
                    font.pixelSize: 20
                    font.bold: true
                    color: root.theme.text
                }
                Label {
                    text: (root.ui.runtimeError || qsTr("No runtime errors")) + (root.ui.runtime.readOnly ? qsTr(" · Data is read-only; back up and inspect damaged files") : "")
                    color: root.theme.text
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                GridLayout {
                    columns: root.availableWidth >= 600 ? 4 : 2
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Proxy port")
                        color: root.theme.text
                    }
                    SpinBox {
                        id: mixedPort
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
                    SpinBox {
                        id: controllerPort
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
                        text: qsTr("Apply ports")
                        enabled: root.ui.settingsReady && !root.ui.runtime.busy && !root.ui.runtime.readOnly
                        onClicked: root.ui.runtimeCommand("ports", [mixedPort.value.toString(), controllerPort.value.toString()])
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Stop and restore proxy")
                        onClicked: root.ui.runtimeCommand("stop")
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
                    text: qsTr("Changing mode, profile or ports briefly restarts the core. After a forced exit, reopening checks and restores proxy settings still owned by QuickClash. You can also check Windows Settings → Network & internet → Proxy. Changes made by other apps are preserved. TUN, providers and Geo are not supported yet.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: qsTr("QuickClash manages mode, loopback addresses, ports, controller and session secret. DNS service, TUN, automatic Geo updates and core selection caching are disabled; profile values are overridden.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                TextArea {
                    visible: !!root.ui.runtime.effectivePreview
                    text: root.ui.runtime.effectivePreview || ""
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
            implicitHeight: appearance.implicitHeight + 40
            ColumnLayout {
                id: appearance
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("Appearance")
                    font.pixelSize: 20
                    font.bold: true
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
                            prominent: root.ui.themeMode === modelData.key
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
            implicitHeight: lifecycle.implicitHeight + 40
            ColumnLayout {
                id: lifecycle
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("Window and tray")
                    font.pixelSize: 20
                    font.bold: true
                    color: root.theme.text
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: root.ui.trayAvailable ? qsTr("Close to the tray; confirmation is required the first time.") : qsTr("No tray is available. Closing keeps the window open so you can minimize or quit.")
                    color: root.theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                CheckBox {
                    id: unloadOption
                    font.family: root.theme.fontFamily
                    text: qsTr("Unload the interface after 5 seconds hidden (experimental)")
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
                    enabled: root.ui.trayAvailable && root.ui.settingsReady
                    onToggled: root.ui.unloadWhenHidden = checked
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
            implicitHeight: replay.implicitHeight + 40
            ColumnLayout {
                id: replay
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("Prototype data")
                    font.pixelSize: 20
                    font.bold: true
                    color: root.theme.text
                }
                CheckBox {
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
