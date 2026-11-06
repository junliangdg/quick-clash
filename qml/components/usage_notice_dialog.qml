pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QuickClash.Ui

ThemedDialog {
    id: control
    required property UiState ui
    property bool acknowledgmentRequired: false
    Component.onCompleted: acknowledgmentRequired = ui.usageNoticePending
    objectName: "usage_notice_dialog"
    anchors.centerIn: parent
    width: Math.min(560, parent.width - 32)
    height: Math.min(560, parent.height - 32)
    implicitHeight: height
    title: qsTr("Usage notice")
    modal: true
    closePolicy: acknowledgmentRequired ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    standardButtons: acknowledgmentRequired ? Dialog.Ok : Dialog.Close
    initialFocusItem: standardButton(acknowledgmentRequired ? Dialog.Ok : Dialog.Close)
    onOpened: {
        const acknowledge = standardButton(Dialog.Ok);
        if (acknowledge) {
            acknowledge.text = qsTr("I understand");
            acknowledge.objectName = "usage_notice_acknowledge";
        }
    }
    onAccepted: ui.acknowledgeUsageNotice()
    contentItem: ThemedScrollView {
        objectName: "usage_notice_scroll"
        theme: control.theme
        form: noticeBody
        Label {
            id: noticeBody
            objectName: "usage_notice_body"
            text: qsTr("QuickClash is an independent graphical client based on the Mihomo (formerly Clash.Meta) core. It manages proxy configurations and subscriptions that you import. Proxy traffic is handled by the bundled official Mihomo core; this project does not develop or modify that core.")
                + "\n\n" + qsTr("QuickClash does not provide remote proxy nodes, node accounts, or subscription services. Supply configurations or subscriptions you are authorized to use and check that their sources are trustworthy.")
                + "\n\n" + qsTr("Enabling system proxy or TUN changes how this device accesses the network. Learn how to stop these features and restore connectivity, and comply with applicable laws and regulations.")
                + "\n\n" + qsTr("QuickClash is released under GPL-3.0-only. Bundled third-party components are covered by their own open-source licenses; see LICENSE in the source and the licenses directory in the distribution.")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: control.theme.text
        }
    }
}
