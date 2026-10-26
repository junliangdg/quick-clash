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
    border.width: selected ? 2 : 0
    border.color: theme.accent
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
        Button {
            id: selectButton
            objectName: "select_subscription"
            Layout.fillWidth: true
            implicitHeight: titleContent.implicitHeight + 12
            enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
            activeFocusOnTab: true
            Accessible.name: root.profile.name || ""
            onClicked: root.activate()
            background: Rectangle {
                color: parent.hovered ? root.theme.accentSurface : "transparent"
                radius: 10
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
                        Layout.alignment: Qt.AlignTop
                        enabled: !root.ui.runtime.busy
                        onClicked: root.manageRequested(root.profileId)
                    }
                }
                Label {
                    text: root.selected ? qsTr("Selected") : qsTr("Click to select")
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
            visible: !!root.profile.secretRef
            text: root.profile.lastUpdateResult === "Failed" ? qsTr("Update failed; the working configuration is kept") : root.profile.pendingApply ? qsTr("Changes waiting to be applied") : qsTr("Downloaded and validated")
            color: root.theme.muted
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
        Flow {
            Layout.fillWidth: true
            spacing: 8
            ActionButton {
                objectName: "update_subscription"
                theme: root.theme
                text: qsTr("Update")
                visible: !!root.profile.secretRef
                enabled: !root.ui.runtime.busy
                onClicked: root.actionRequested("update", root.profileId)
            }
        }
    }
}
