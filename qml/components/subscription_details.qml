pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ColumnLayout {
    id: root
    required property ThemeTokens theme
    property var profile: ({})
    property bool overview: false
    readonly property var status: profile.usageStatus || ({})
    readonly property bool completeUsage: status.complete === true
    readonly property real usedBytes: completeUsage ? Number(status.used) : 0
    readonly property real totalBytes: Number(status.quota || 0)
    readonly property string updatedTime: profile.updatedAt ? dateText(profile.updatedAt) : qsTr("Unknown")
    readonly property string collectedTime: profile.usageCollectedAt ? dateText(profile.usageCollectedAt) : qsTr("Unknown")
    readonly property bool sameDisplayedTime: !!profile.updatedAt && !!profile.usageCollectedAt && updatedTime !== qsTr("Unknown") && collectedTime !== qsTr("Unknown") && updatedTime === collectedTime
    function dateText(value): string {
        const date = new Date(value);
        return isNaN(date.getTime()) ? qsTr("Unknown") : date.toLocaleString(Qt.locale(), Locale.ShortFormat);
    }
    function sizeText(value): string {
        if (value >= 1073741824)
            return (value / 1073741824).toFixed(2) + " GiB";
        return (value / 1048576).toFixed(1) + " MiB";
    }
    spacing: 6
    Label {
        objectName: "subscription_updated_card"
        visible: !root.overview
        text: qsTr("Updated: %1").arg(root.updatedTime)
        color: root.updatedTime === qsTr("Unknown") ? root.theme.muted : root.theme.text
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
    }
    Label {
        objectName: "subscription_usage"
        visible: !root.overview
        text: root.completeUsage ? qsTr("Used %1 / %2").arg(root.sizeText(root.usedBytes)).arg(root.totalBytes > 0 ? root.sizeText(root.totalBytes) : qsTr("Not provided")) : qsTr("Usage not provided")
        color: root.completeUsage ? root.theme.text : root.theme.muted
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
    GridLayout {
        visible: root.overview
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        columns: width >= remainingLabel.implicitWidth + remainingValue.implicitWidth + columnSpacing ? 2 : 1
        Label {
            id: remainingLabel
            text: qsTr("Remaining")
            color: root.theme.muted
            font.pixelSize: root.theme.scaledSize(12)
            Layout.fillWidth: true
        }
        Label {
            id: remainingValue
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            wrapMode: Text.Wrap
            text: root.completeUsage ? (root.totalBytes > 0 ? root.sizeText(Math.max(0, root.totalBytes - root.usedBytes)) : qsTr("Not provided")) : qsTr("Usage not provided")
            color: root.completeUsage && root.totalBytes > 0 ? (root.status.quotaState === "Exhausted" ? root.theme.error : root.status.quotaState === "Low" ? root.theme.warning : root.theme.text) : root.theme.muted
            font.pixelSize: root.theme.scaledSize(19)
            font.weight: Font.DemiBold
        }
    }
    ThemedProgress {
        objectName: "subscription_usage_progress"
        theme: root.theme
        Layout.fillWidth: true
        visible: root.completeUsage && root.totalBytes > 0
        from: 0
        to: 1
        value: Math.min(1, Math.max(0, root.usedBytes / root.totalBytes))
    }
    Label {
        visible: root.overview && root.completeUsage
        text: qsTr("Used %1 / %2").arg(root.sizeText(root.usedBytes)).arg(root.totalBytes > 0 ? root.sizeText(root.totalBytes) : qsTr("Not provided"))
        color: root.theme.muted
        font.pixelSize: root.theme.supportingSize
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
    Label {
        objectName: "subscription_expiry"
        text: qsTr("Expires: %1").arg(root.status.expiresAt ? root.dateText(root.status.expiresAt) : qsTr("Not provided"))
        color: root.status.expiresAt ? root.theme.text : root.theme.muted
        font.pixelSize: root.overview ? root.theme.scaledSize(12) : root.theme.bodySize
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
    Label {
        objectName: "subscription_usage_alert"
        visible: root.status.quotaState === "Low" || root.status.quotaState === "Exhausted" || root.status.expiryState === "Soon" || root.status.expiryState === "Expired"
        text: [root.status.quotaState === "Exhausted" ? qsTr("Subscription quota exhausted") : root.status.quotaState === "Low" ? qsTr("10% quota remaining or less") : "", root.status.expiryState === "Expired" ? qsTr("Subscription expired") : root.status.expiryState === "Soon" ? qsTr("Expires within 7 days") : ""].filter(value => value.length > 0).join(" · ")
        color: root.status.quotaState === "Exhausted" || root.status.expiryState === "Expired" ? root.theme.error : root.theme.warning
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
    Label {
        objectName: "subscription_usage_collected"
        visible: !!root.profile.secretRef && !root.sameDisplayedTime
        text: qsTr("Usage collected: %1").arg(root.collectedTime)
        color: root.theme.muted
        font.pixelSize: root.overview ? root.theme.scaledSize(12) : root.theme.supportingSize
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
    Label {
        visible: !root.overview && !!root.profile.secretRef
        text: qsTr("Provider totals may be delayed and include other devices.")
        color: root.theme.muted
        font.pixelSize: root.theme.supportingSize
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
    Label {
        objectName: "subscription_updated_overview"
        visible: root.overview
        text: qsTr("Updated: %1").arg(root.updatedTime)
        color: root.theme.muted
        font.pixelSize: root.theme.scaledSize(12)
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
    }
}
