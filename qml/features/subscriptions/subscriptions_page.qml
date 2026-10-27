pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QuickClash.Ui

ListView {
    id: root
    objectName: "subscriptions_page"
    required property UiState ui
    required property ThemeTokens theme
    property string managedId: ""
    property var managedProfile: ({})
    property string importName: ""
    readonly property real availableWidth: Math.max(0, width - (scrollBar.visible ? scrollBar.width : 0))
    readonly property int cardColumns: availableWidth >= 900 ? 3 : availableWidth >= 580 ? 2 : 1
    function scrollToRow(row) {
        positionViewAtIndex(row, ListView.Beginning);
    }
    function focusRow(row) {
        if (count === 0)
            return;
        currentIndex = Math.max(0, Math.min(count - 1, row));
        positionViewAtIndex(currentIndex, ListView.Contain);
        Qt.callLater(function () {
            if (root.currentItem)
                root.currentItem.focusFirst();
        });
    }
    Keys.onDownPressed: focusRow(currentIndex + 1)
    Keys.onUpPressed: focusRow(currentIndex - 1)
    function refreshManaged() {
        const previousName = managedProfile.name || "";
        managedProfile = ui.profileModel.profile(managedId);
        if (!managedProfile.id) {
            rollbackConfirmation.close();
            deleteConfirmation.close();
            management.close();
            managedId = "";
        } else if (itemName.text === previousName || !itemName.activeFocus) {
            itemName.text = managedProfile.name || "";
        }
    }
    function manage(profileId) {
        managedId = profileId;
        refreshManaged();
        if (managedId)
            management.open();
    }
    Connections {
        target: root.ui.profileModel
        function onProfileChanged(profileId: string) {
            if (profileId === root.managedId)
                root.refreshManaged();
        }
    }
    onCardColumnsChanged: ui.profileRows.columns = cardColumns
    Component.onCompleted: ui.profileRows.columns = cardColumns
    model: ui.profileRows
    reuseItems: true
    cacheBuffer: 0
    spacing: 12
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {
        id: scrollBar
    }
    clip: true
    FileDialog {
        id: fileDialog
        title: qsTr("Import Clash Meta (mihomo) profile")
        nameFilters: [qsTr("YAML profiles (*.yaml *.yml)"), qsTr("All files (*)")]
        onAccepted: root.ui.runtimeCommand("import", [selectedFile.toString(), root.importName])
    }
    ThemedDialog {
        theme: root.theme
        id: rollbackConfirmation
        parent: Overlay.overlay
        title: qsTr("Restore the previous subscription version?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.ui.runtimeCommand("rollback_profile", [root.managedId, "confirmed"])
    }
    ThemedDialog {
        theme: root.theme
        id: deleteConfirmation
        parent: Overlay.overlay
        title: qsTr("Delete this subscription?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: Math.min(340, root.availableWidth - 60)
            text: qsTr("Deleting the selected subscription restores network settings and stops its session. Other subscriptions are kept.")
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            management.close();
            root.ui.runtimeCommand("delete", [root.managedId]);
        }
    }
    ThemedDialog {
        theme: root.theme
        id: management
        parent: Overlay.overlay
        objectName: "subscription_management"
        title: qsTr("Subscription settings")
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, root.availableWidth - 16)
        height: Math.min(650, root.height - 24)
        standardButtons: Dialog.Close
        onOpened: {
            itemName.text = root.managedProfile.name || "";
            sourceUrl.clear();
            sourceHeaders.clear();
        }
        contentItem: ScrollView {
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.availableWidth
                spacing: 12
                ThemedField {
                    theme: root.theme
                    id: itemName
                    objectName: "subscription_name"
                    Layout.fillWidth: true
                    maximumLength: 80
                    placeholderText: qsTr("Name")
                }
                ActionButton {
                    theme: root.theme
                    text: qsTr("Rename")
                    enabled: !root.ui.runtime.busy && itemName.text.trim().length > 0
                    onClicked: root.ui.runtimeCommand("rename", [root.managedId, itemName.text])
                }
                ThemedComboBox {
                    theme: root.theme
                    id: updateInterval
                    visible: !!root.managedProfile.secretRef
                    Layout.fillWidth: true
                    enabled: !root.ui.runtime.busy
                    property var minutes: [0, 60, 360, 720, 1440]
                    model: [qsTr("Manual updates"), qsTr("Every hour"), qsTr("Every 6 hours"), qsTr("Every 12 hours"), qsTr("Every day")]
                    currentIndex: Math.max(0, minutes.indexOf(root.managedProfile.updateIntervalMinutes || 0))
                    onActivated: root.ui.runtimeCommand("subscription_policy", [root.managedId, minutes[currentIndex].toString(), root.managedProfile.autoApply ? "true" : "false"])
                }
                ThemedSwitch {
                    theme: root.theme
                    visible: !!root.managedProfile.secretRef
                    text: qsTr("Apply updates when selected")
                    checked: !!root.managedProfile.autoApply
                    enabled: !root.ui.runtime.busy
                    palette.windowText: root.theme.text
                    onClicked: root.ui.runtimeCommand("subscription_policy", [root.managedId, (root.managedProfile.updateIntervalMinutes || 0).toString(), checked ? "true" : "false"])
                }
                ThemedField {
                    theme: root.theme
                    id: sourceUrl
                    objectName: "subscription_source_url"
                    Layout.fillWidth: true
                    placeholderText: qsTr("New HTTPS URL (leave blank to keep source)")
                    echoMode: TextInput.Password
                    maximumLength: 8192
                }
                ThemedField {
                    theme: root.theme
                    id: sourceHeaders
                    objectName: "subscription_source_headers"
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
                        sourceUrl.clear();
                        sourceHeaders.clear();
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
                    destructive: true
                    enabled: !root.ui.runtime.busy
                    onClicked: deleteConfirmation.open()
                }
            }
        }
    }
    header: ColumnLayout {
        width: root.availableWidth
        spacing: root.theme.gap
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: form.implicitHeight + 32
            ColumnLayout {
                id: form
                anchors.fill: parent
                anchors.margins: root.theme.panelPadding
                spacing: 10
                Label {
                    text: qsTr("Import")
                    color: root.theme.text
                    font.pixelSize: root.theme.bodySize
                    font.weight: Font.DemiBold
                }
                ThemedField {
                    theme: root.theme
                    id: profileName
                    objectName: "import_profile_name"
                    placeholderText: qsTr("Name (optional)")
                    Layout.fillWidth: true
                    maximumLength: 80
                    onTextChanged: root.importName = text
                }
                ThemedField {
                    theme: root.theme
                    id: subscriptionUrl
                    objectName: "subscription_url"
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
                        pending: !!root.ui.runtime.busy
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
                    text: qsTr("Standard HTTPS subscriptions request Clash Meta (mihomo) YAML. The first valid import is selected automatically. Click a subscription card to switch its groups and running configuration.")
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
        Label {
            text: qsTr("Subscriptions")
            color: root.theme.text
            font.pixelSize: root.theme.bodySize
            font.weight: Font.DemiBold
        }
        Label {
            visible: !root.ui.profileModel.count
            text: qsTr("No subscriptions yet")
            color: root.theme.muted
        }
        Item {
            implicitHeight: 2
            Layout.fillWidth: true
        }
    }
    delegate: Row {
        id: cardRow
        required property var first
        required property var second
        required property var third
        width: root.availableWidth
        height: Math.max(firstCard.implicitHeight, secondCard.height, thirdCard.height)
        function focusFirst() {
            firstCard.focusSelect();
        }
        spacing: 12
        readonly property real cardWidth: Math.max(0, (width - spacing * (root.cardColumns - 1)) / root.cardColumns)
        SubscriptionCard {
            id: firstCard
            width: cardRow.cardWidth
            height: implicitHeight
            ui: root.ui
            theme: root.theme
            profile: cardRow.first
            onManageRequested: profileId => root.manage(profileId)
            onActionRequested: (command, profileId) => root.ui.runtimeCommand(command, [profileId])
        }
        Loader {
            id: secondCard
            active: !!cardRow.second.id
            visible: active
            width: cardRow.cardWidth
            height: item ? item.implicitHeight : 0
            sourceComponent: SubscriptionCard {
                ui: root.ui
                theme: root.theme
                profile: cardRow.second
                onManageRequested: profileId => root.manage(profileId)
                onActionRequested: (command, profileId) => root.ui.runtimeCommand(command, [profileId])
            }
        }
        Loader {
            id: thirdCard
            active: !!cardRow.third.id
            visible: active
            width: cardRow.cardWidth
            height: item ? item.implicitHeight : 0
            sourceComponent: SubscriptionCard {
                ui: root.ui
                theme: root.theme
                profile: cardRow.third
                onManageRequested: profileId => root.manage(profileId)
                onActionRequested: (command, profileId) => root.ui.runtimeCommand(command, [profileId])
            }
        }
    }
}
