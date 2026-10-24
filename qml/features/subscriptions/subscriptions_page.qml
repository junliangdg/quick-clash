pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QuickClash.Ui

ScrollView {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    contentWidth: availableWidth
    clip: true
    FileDialog {
        id: fileDialog
        title: qsTr("Import Mihomo profile")
        nameFilters: [qsTr("YAML profiles (*.yaml *.yml)"), qsTr("All files (*)")]
        onAccepted: root.ui.runtimeCommand("import", [selectedFile.toString(), profileName.text])
    }
    ColumnLayout {
        width: root.availableWidth
        spacing: 14
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: form.implicitHeight + 40
            ColumnLayout {
                id: form
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                Label {
                    text: qsTr("Add subscription or profile")
                    color: root.theme.text
                    font.pixelSize: 20
                    font.bold: true
                }
                TextField {
                    id: profileName
                    placeholderText: qsTr("Name (optional)")
                    Layout.fillWidth: true
                    maximumLength: 80
                }
                TextField {
                    id: subscriptionUrl
                    placeholderText: qsTr("HTTPS subscription URL")
                    Layout.fillWidth: true
                    echoMode: TextInput.PasswordEchoOnEdit
                    maximumLength: 8192
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    ActionButton {
                        theme: root.theme
                        text: root.ui.runtime.busy ? qsTr("Working…") : qsTr("Add subscription")
                        enabled: !root.ui.runtime.busy && root.ui.settingsReady && subscriptionUrl.text.length > 0
                        prominent: true
                        onClicked: {
                            root.ui.runtimeCommand("subscribe", [subscriptionUrl.text, profileName.text]);
                            subscriptionUrl.clear();
                        }
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Import file")
                        enabled: !root.ui.runtime.busy && root.ui.settingsReady
                        onClicked: fileDialog.open()
                    }
                }
                Label {
                    text: qsTr("Standard subscription URLs automatically request Clash/Mihomo YAML over a direct connection. After validation, select Use to apply. Supports inline HTTP, SOCKS5, Shadowsocks and select groups. Base64, share links, providers, Geo and TUN are not supported yet.")
                    color: root.theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: root.ui.runtimeError
                    visible: text.length > 0
                    color: root.theme.text
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
        Repeater {
            model: root.ui.runtime.profiles || []
            delegate: FlatTile {
                id: profileTile
                required property var modelData
                theme: root.theme
                Layout.fillWidth: true
                implicitHeight: profileContent.implicitHeight + 36
                ColumnLayout {
                    id: profileContent
                    anchors.fill: parent
                    anchors.margins: 18
                    TextField {
                        id: itemName
                        text: profileTile.modelData.name
                        maximumLength: 80
                        Layout.fillWidth: true
                    }
                    Label {
                        text: (profileTile.modelData.id === root.ui.runtime.selectedId ? qsTr("Selected · ") : "") + (profileTile.modelData.secretRef ? qsTr("HTTPS subscription · URL stored encrypted") : qsTr("Local profile"))
                        color: root.theme.muted
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        text: profileTile.modelData.updatedAt
                        color: root.theme.muted
                    }
                    TextField {
                        id: sourceUrl
                        placeholderText: qsTr("New HTTPS URL (leave blank to keep source)")
                        echoMode: TextInput.Password
                        maximumLength: 8192
                        Layout.fillWidth: true
                    }
                    TextField {
                        id: sourceHeaders
                        placeholderText: qsTr("Headers as JSON: Authorization, User-Agent, Accept")
                        echoMode: TextInput.Password
                        maximumLength: 8192
                        Layout.fillWidth: true
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        ActionButton {
                            theme: root.theme
                            text: qsTr("Use")
                            enabled: !root.ui.runtime.busy
                            prominent: true
                            onClicked: root.ui.runtimeCommand("select", [profileTile.modelData.id])
                        }
                        ActionButton {
                            theme: root.theme
                            text: qsTr("Update")
                            visible: !!profileTile.modelData.secretRef
                            enabled: !root.ui.runtime.busy
                            onClicked: root.ui.runtimeCommand("update", [profileTile.modelData.id])
                        }
                        ActionButton {
                            theme: root.theme
                            text: qsTr("Rename")
                            enabled: !root.ui.runtime.busy
                            onClicked: root.ui.runtimeCommand("rename", [profileTile.modelData.id, itemName.text])
                        }
                        ActionButton {
                            theme: root.theme
                            text: profileTile.modelData.id === root.ui.runtime.selectedId ? qsTr("Stop and delete") : qsTr("Delete")
                            enabled: !root.ui.runtime.busy
                            onClicked: root.ui.runtimeCommand("delete", [profileTile.modelData.id])
                        }
                        ActionButton {
                            theme: root.theme
                            text: qsTr("Validate and replace source")
                            enabled: !root.ui.runtime.busy && sourceUrl.text.length > 0
                            onClicked: {
                                root.ui.runtimeCommand("source", [profileTile.modelData.id, sourceUrl.text, sourceHeaders.text]);
                                sourceUrl.clear();
                                sourceHeaders.clear();
                            }
                        }
                    }
                }
            }
        }
    }
}
