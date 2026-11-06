pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

FlatTile {
    id: root
    objectName: "subscription_card"
    required property UiState ui
    required property var profile
    readonly property string profileId: profile.id || ""
    readonly property bool selected: profileId === ui.runtime.selectedId
    readonly property bool updateFailed: profile.lastUpdateResult === "Failed"
    readonly property bool applyFailed: !!profile.pendingApply && ((updateFailed && profile.lastUpdateSaved === true) || !!profile.lastApplyError)
    readonly property bool updating: !!ui.runtime.busy && ui.runtime.updatingSubscriptionId === profileId
    signal manageRequested(string profileId)
    signal actionRequested(string command, string profileId)
    function focusSelect() {
        selectButton.forceActiveFocus(Qt.TabFocusReason);
    }
    function activate() {
        if (profileId && (!selected || profile.pendingApply))
            actionRequested("select", profileId);
    }
    implicitHeight: cardContent.implicitHeight + 28
    border.width: 1
    border.color: selected ? theme.accent : theme.border
    // Keep background selection below interactive children; buttons never bubble into it.
    MouseArea {
        anchors.fill: parent
        enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
        onClicked: root.activate()
    }
    ColumnLayout {
        id: cardContent
        z: 1
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 8
        ActionButton {
            theme: root.theme
            id: selectButton
            objectName: "select_subscription"
            Layout.fillWidth: true
            implicitHeight: titleContent.implicitHeight + 12
            enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
            activeFocusOnTab: true
            Accessible.name: root.profile.name || ""
            onClicked: root.activate()
            background: StateSurface {
                theme: root.theme
                hovered: selectButton.hovered
                down: selectButton.down
                focused: selectButton.activeFocus
            }
            contentItem: ColumnLayout {
                id: titleContent
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        text: root.profile.name || ""
                        color: root.theme.text
                        font.bold: true
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                    }
                    // Management entry lives on the title row, top-right of the card.
                    ActionButton {
                        objectName: "manage_subscription"
                        theme: root.theme
                        text: qsTr("Manage")
                        ghost: true
                        Layout.alignment: Qt.AlignTop
                        enabled: !root.ui.runtime.busy
                        onClicked: root.manageRequested(root.profileId)
                    }
                }
                Label {
                    text: root.selected ? qsTr("Selected") : qsTr("Use this subscription")
                    color: root.selected ? root.theme.accent : root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: root.profile.secretRef ? qsTr("HTTPS subscription") : qsTr("Local profile")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
        SubscriptionDetails {
            Layout.fillWidth: true
            theme: root.theme
            profile: root.profile
        }
        Label {
            objectName: "subscription_update_status"
            visible: !!root.profile.secretRef
            text: root.updating ? qsTr("Updating…") : root.updateFailed ? (root.profile.lastUpdateSaved ? qsTr("New configuration saved; applying failed") : qsTr("Update failed; the previous configuration is kept")) : root.profile.pendingApply ? (root.selected ? qsTr("New configuration downloaded; the previous version is in use") : qsTr("Updated; available when this subscription is selected")) : root.profile.lastUpdateResult === "Updated" ? (root.profile.lastUpdateChanged === false ? qsTr("Already up to date") : root.selected ? qsTr("Updated and applied") : qsTr("Updated")) : qsTr("Downloaded and validated")
            color: root.updateFailed ? root.theme.error : root.profile.pendingApply && root.selected ? root.theme.warning : root.theme.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            objectName: "subscription_update_error"
            visible: root.updateFailed && !!root.profile.lastUpdateError
            text: root.ui.errorText(root.profile.lastUpdateError || "")
            color: root.theme.error
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            objectName: "subscription_apply_error"
            visible: !!root.profile.pendingApply && !!root.profile.lastApplyError
            text: qsTr("Applying failed: %1").arg(root.ui.errorText(root.profile.lastApplyError || ""))
            color: root.theme.error
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            objectName: "subscription_pending_status"
            visible: root.updateFailed && !!root.profile.pendingApply && !root.profile.lastUpdateSaved
            text: root.selected ? qsTr("A previously downloaded configuration is still waiting to be applied") : qsTr("A previously downloaded configuration is available when selected")
            color: root.theme.warning
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            visible: !!root.profile.nextUpdateAt && !!root.profile.updateIntervalMinutes
            text: qsTr("Next update: %1").arg(new Date(root.profile.nextUpdateAt || 0).toLocaleString(Qt.locale()))
            color: root.theme.muted
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            visible: root.selected && !!root.ui.runtime.ready
            text: qsTr("Applying an update may briefly interrupt connections.")
            color: root.theme.muted
            font.pixelSize: root.theme.supportingSize
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            ActionButton {
                width: Math.min(implicitWidth, parent.width)
                objectName: "update_subscription"
                theme: root.theme
                text: root.updating ? qsTr("Updating…") : root.updateFailed && !root.applyFailed ? qsTr("Retry update") : qsTr("Update")
                pending: root.updating
                visible: !!root.profile.secretRef
                enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
                onClicked: root.actionRequested("update", root.profileId)
            }
            ActionButton {
                width: Math.min(implicitWidth, parent.width)
                objectName: "apply_subscription"
                theme: root.theme
                visible: !root.selected || !!root.profile.pendingApply
                text: root.selected ? (root.applyFailed ? qsTr("Retry applying") : qsTr("Apply now")) : qsTr("Use this subscription")
                enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
                onClicked: root.actionRequested("select", root.profileId)
            }
        }
    }
}
