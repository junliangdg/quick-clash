pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ColumnLayout {
    id: root
    required property UiState ui
    required property ThemeTokens theme
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
