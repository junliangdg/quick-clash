pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ColumnLayout {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    readonly property string sourceStateText: ui.sourceState === "clean" ? qsTr("Clean") : ui.sourceState === "dirty" ? qsTr("Modified") : qsTr("Unknown")
    spacing: theme.gap
    objectName: "settings_about"
    FlatTile {
        theme: root.theme
        Layout.fillWidth: true
        implicitHeight: aboutContent.implicitHeight + 2 * root.theme.panelPadding
        ColumnLayout {
            id: aboutContent
            anchors.fill: parent
            anchors.margins: root.theme.panelPadding
            spacing: 8
            Label {
                objectName: "application_about_heading"
                text: qsTranslate("settings_page", "About")
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.bodySize
                font.weight: Font.DemiBold
                color: root.theme.text
            }
            Label {
                text: "QuickClash"
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.titleSize
                color: root.theme.text
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Label {
                objectName: "application_version"
                Layout.fillWidth: true
                text: qsTranslate("settings_page", "Version: %1").arg(Qt.application.version)
                font.family: root.theme.fontFamily
                color: root.theme.muted
                wrapMode: Text.WordWrap
            }
            Label {
                objectName: "application_commit"
                Layout.fillWidth: true
                text: qsTr("Commit: %1").arg(root.ui.sourceCommit ? root.ui.sourceCommit.slice(0, 12) : qsTr("Unknown"))
                font.family: root.theme.fontFamily
                color: root.theme.muted
                wrapMode: Text.WrapAnywhere
            }
            Label {
                objectName: "application_source_state"
                Layout.fillWidth: true
                text: qsTr("Source state: %1").arg(root.sourceStateText)
                font.family: root.theme.fontFamily
                color: root.theme.muted
                wrapMode: Text.WordWrap
            }
            ActionButton {
                objectName: "application_copy_version"
                theme: root.theme
                text: qsTr("Copy version info")
                onClicked: root.ui.copyText("QuickClash " + Qt.application.version + "\n" + qsTr("Commit: %1").arg(root.ui.sourceCommit || qsTr("Unknown")) + "\n" + qsTr("Source state: %1").arg(root.sourceStateText))
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("An independent graphical client based on the Mihomo (formerly Clash.Meta) core.")
                color: root.theme.muted
                wrapMode: Text.WordWrap
            }
            ActionButton {
                objectName: "settings_usage_notice"
                theme: root.theme
                text: qsTr("Usage notice")
                onClicked: root.ui.showUsageNotice()
            }
        }
    }
}
