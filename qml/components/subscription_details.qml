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
    readonly property var usage: profile.usage || ({})
    readonly property real usedBytes: Number(usage.upload || 0) + Number(usage.download || 0)
    readonly property real totalBytes: Number(usage.total || 0)
    function dateText(value): string {
        const date = new Date(value);
        return isNaN(date.getTime()) ? qsTr("Unknown") : date.toLocaleString(Qt.locale());
    }
    function sizeText(value): string {
        if (value >= 1073741824)
            return (value / 1073741824).toFixed(2) + " GiB";
        return (value / 1048576).toFixed(1) + " MiB";
    }
    spacing: 6
    Label {
        visible: !root.overview
        text: qsTr("Updated: %1").arg(root.profile.updatedAt ? root.dateText(root.profile.updatedAt) : qsTr("Unknown"))
        color: root.theme.muted
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
    }
    Label {
        visible: !root.overview
        text: root.usage.upload !== undefined || root.usage.download !== undefined
            ? qsTr("Used %1 / %2").arg(root.sizeText(root.usedBytes)).arg(root.totalBytes > 0 ? root.sizeText(root.totalBytes) : qsTr("Not provided"))
            : qsTr("Usage not provided")
        color: root.theme.text
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
    RowLayout {
        visible: root.overview
        Layout.fillWidth: true
        Label { text: qsTr("Current usage"); color: root.theme.muted; font.pixelSize: 12; Layout.fillWidth: true }
        Label {
            text: root.usage.upload !== undefined || root.usage.download !== undefined
                ? root.sizeText(root.usedBytes) + " / " + (root.totalBytes > 0 ? root.sizeText(root.totalBytes) : qsTr("Not provided"))
                : qsTr("Usage not provided")
            color: root.theme.muted
            font.pixelSize: 12
        }
    }
    ThemedProgress {
        theme: root.theme
        Layout.fillWidth: true
        visible: root.totalBytes > 0
        from: 0
        to: 1
        value: Math.min(1, Math.max(0, root.usedBytes / root.totalBytes))
    }
    Label {
        text: qsTr("Expires: %1").arg(Number(root.usage.expire || 0) > 0 ? root.dateText(Number(root.usage.expire) * 1000) : qsTr("Not provided"))
        color: root.theme.muted
        font.pixelSize: root.overview ? 12 : root.theme.bodySize
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
    Label {
        visible: root.overview
        text: qsTr("Updated: %1").arg(root.profile.updatedAt ? root.dateText(root.profile.updatedAt) : qsTr("Unknown"))
        color: root.theme.muted
        font.pixelSize: 12
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
    }
}
