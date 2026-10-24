import QtQuick
import QuickClash.Ui

Item {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    DeferredContent {
        width: parent.width
        theme: root.theme
        heading: qsTr("Rules are planned for P3")
        description: qsTr("Rules and providers will appear in evaluation order and load while visible.")
        milestone: qsTr("No live rule data is available yet.")
    }
}
