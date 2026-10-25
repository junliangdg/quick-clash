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
    property string managedId: ""
    readonly property var managedProfile: ui.profiles.find(profile => profile.id === managedId) || ({})
    contentWidth: availableWidth
    clip: true
    FileDialog {
        id: fileDialog
        title: qsTr("Import Mihomo profile")
        nameFilters: [qsTr("YAML profiles (*.yaml *.yml)"), qsTr("All files (*)")]
        onAccepted: root.ui.runtimeCommand("import", [selectedFile.toString(), profileName.text])
    }
    Dialog {
        id: rollbackConfirmation
        title: qsTr("Restore the previous subscription version?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.ui.runtimeCommand("rollback_profile", [root.managedId, "confirmed"])
    }
    Dialog {
        id: deleteConfirmation
        title: qsTr("Delete this subscription?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: Math.min(340, root.availableWidth - 60)
            text: qsTr("Deleting the selected subscription restores network settings and stops its session. Other subscriptions are kept.")
            wrapMode: Text.WordWrap
        }
        onAccepted: { management.close(); root.ui.runtimeCommand("delete", [root.managedId]); }
    }
    Dialog {
        id: management
        title: qsTr("Subscription settings")
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, root.availableWidth - 16)
        height: Math.min(650, root.height - 24)
        standardButtons: Dialog.Close
        onOpened: { itemName.text = root.managedProfile.name || ""; sourceUrl.clear(); sourceHeaders.clear(); }
        contentItem: ScrollView {
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.availableWidth
                spacing: 12
                TextField { id: itemName; Layout.fillWidth: true; maximumLength: 80; placeholderText: qsTr("Name") }
                ActionButton {
                    theme: root.theme
                    text: qsTr("Rename")
                    enabled: !root.ui.runtime.busy && itemName.text.trim().length > 0
                    onClicked: root.ui.runtimeCommand("rename", [root.managedId, itemName.text])
                }
                ComboBox {
                    id: updateInterval
                    visible: !!root.managedProfile.secretRef
                    Layout.fillWidth: true
                    enabled: !root.ui.runtime.busy
                    property var minutes: [0, 60, 360, 720, 1440]
                    model: [qsTr("Manual updates"), qsTr("Every hour"), qsTr("Every 6 hours"), qsTr("Every 12 hours"), qsTr("Every day")]
                    currentIndex: Math.max(0, minutes.indexOf(root.managedProfile.updateIntervalMinutes || 0))
                    onActivated: root.ui.runtimeCommand("subscription_policy", [root.managedId, minutes[currentIndex].toString(), root.managedProfile.autoApply ? "true" : "false"])
                }
                Switch {
                    visible: !!root.managedProfile.secretRef
                    text: qsTr("Apply updates when selected")
                    checked: !!root.managedProfile.autoApply
                    enabled: !root.ui.runtime.busy
                    palette.windowText: root.theme.text
                    onClicked: root.ui.runtimeCommand("subscription_policy", [root.managedId, (root.managedProfile.updateIntervalMinutes || 0).toString(), checked ? "true" : "false"])
                }
                TextField {
                    id: sourceUrl
                    Layout.fillWidth: true
                    placeholderText: qsTr("New HTTPS URL (leave blank to keep source)")
                    echoMode: TextInput.Password
                    maximumLength: 8192
                }
                TextField {
                    id: sourceHeaders
                    Layout.fillWidth: true
                    placeholderText: qsTr("Headers as JSON: Authorization, User-Agent, Accept")
                    echoMode: TextInput.Password
                    maximumLength: 8192
                }
                ActionButton {
                    theme: root.theme
                    text: qsTr("Validate and replace source")
                    enabled: !root.ui.runtime.busy && sourceUrl.text.length > 0
                    onClicked: {
                        root.ui.runtimeCommand("source", [root.managedId, sourceUrl.text, sourceHeaders.text]);
                        sourceUrl.clear(); sourceHeaders.clear();
                    }
                }
                ActionButton {
                    theme: root.theme
                    text: qsTr("Restore previous version…")
                    visible: (root.managedProfile.history || []).length > 0
                    enabled: !root.ui.runtime.busy
                    onClicked: rollbackConfirmation.open()
                }
                ActionButton {
                    theme: root.theme
                    text: qsTr("Delete")
                    enabled: !root.ui.runtime.busy
                    onClicked: deleteConfirmation.open()
                }
            }
        }
    }
    ColumnLayout {
        width: root.availableWidth
        spacing: 14
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: form.implicitHeight + 36
            ColumnLayout {
                id: form
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10
                Label { text: qsTr("Import"); color: root.theme.text; font.pixelSize: 20; font.bold: true }
                TextField { id: profileName; placeholderText: qsTr("Name (optional)"); Layout.fillWidth: true; maximumLength: 80 }
                TextField { id: subscriptionUrl; placeholderText: qsTr("HTTPS subscription URL"); Layout.fillWidth: true; echoMode: TextInput.PasswordEchoOnEdit; maximumLength: 8192 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    ActionButton {
                        theme: root.theme
                        text: root.ui.runtime.busy ? qsTr("Working…") : qsTr("Add subscription")
                        enabled: !root.ui.runtime.busy && root.ui.settingsReady && subscriptionUrl.text.length > 0
                        prominent: true
                        onClicked: { root.ui.runtimeCommand("subscribe", [subscriptionUrl.text, profileName.text]); subscriptionUrl.clear(); }
                    }
                    ActionButton { theme: root.theme; text: qsTr("Import file"); enabled: !root.ui.runtime.busy && root.ui.settingsReady; onClicked: fileDialog.open() }
                }
                Label {
                    text: qsTr("Standard HTTPS subscriptions request Clash/Mihomo YAML. The first valid import is selected automatically. Click a subscription card to switch its groups and running configuration.")
                    color: root.theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
        Label {
            text: root.ui.runtimeError
            visible: text.length > 0
            color: root.theme.text
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label { text: qsTr("Subscriptions"); color: root.theme.text; font.pixelSize: 20; font.bold: true }
        Label { visible: !root.ui.profiles.length; text: qsTr("No subscriptions yet"); color: root.theme.muted }
        GridLayout {
            Layout.fillWidth: true
            columns: root.availableWidth >= 900 ? 3 : root.availableWidth >= 580 ? 2 : 1
            columnSpacing: 12
            rowSpacing: 12
            Repeater {
                model: root.ui.profiles
                delegate: FlatTile {
                    id: tile
                    required property var modelData
                    readonly property bool selected: modelData.id === root.ui.runtime.selectedId
                    theme: root.theme
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    implicitHeight: cardContent.implicitHeight + 28
                    border.width: selected ? 2 : 0
                    border.color: root.theme.accent
                    ColumnLayout {
                        id: cardContent
                        z: 1
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8
                        Button {
                            Layout.fillWidth: true
                            implicitHeight: titleContent.implicitHeight + 12
                            enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
                            Accessible.name: tile.modelData.name
                            onClicked: {
                                if (!tile.selected || tile.modelData.pendingApply)
                                    root.ui.runtimeCommand("select", [tile.modelData.id]);
                            }
                            background: Rectangle { color: parent.hovered ? root.theme.accentSurface : "transparent"; radius: 10 }
                            contentItem: ColumnLayout {
                                id: titleContent
                                Label { text: tile.modelData.name; color: root.theme.text; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText }
                                Label { text: tile.selected ? qsTr("Selected") : qsTr("Click to select"); color: tile.selected ? root.theme.accent : root.theme.muted }
                                Label { text: tile.modelData.secretRef ? qsTr("HTTPS subscription") : qsTr("Local profile"); color: root.theme.muted }
                            }
                        }
                        SubscriptionDetails { Layout.fillWidth: true; theme: root.theme; profile: tile.modelData }
                        Label {
                            visible: !!tile.modelData.secretRef
                            text: tile.modelData.lastUpdateResult === "Failed" ? qsTr("Update failed; the working configuration is kept") : tile.modelData.pendingApply ? qsTr("Changes waiting to be applied") : qsTr("Downloaded and validated")
                            color: root.theme.muted
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            visible: !!tile.modelData.nextUpdateAt && !!tile.modelData.updateIntervalMinutes
                            text: qsTr("Next update: %1").arg(new Date(tile.modelData.nextUpdateAt || 0).toLocaleString(Qt.locale()))
                            color: root.theme.muted
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            ActionButton { theme: root.theme; text: qsTr("Update"); visible: !!tile.modelData.secretRef; enabled: !root.ui.runtime.busy; onClicked: root.ui.runtimeCommand("update", [tile.modelData.id]) }
                            ActionButton { theme: root.theme; text: qsTr("Manage"); enabled: !root.ui.runtime.busy; onClicked: { root.managedId = tile.modelData.id; management.open(); } }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
                        onClicked: { if (!tile.selected || tile.modelData.pendingApply) root.ui.runtimeCommand("select", [tile.modelData.id]); }
                    }
                }
            }
        }
    }
}
