import QtQuick
import QuickClash.Ui

Item {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    DeferredContent {
        width: parent.width
        theme: root.theme
        heading: qsTr("规则页将在 P3 交付")
        description: qsTr("按实际顺序查看规则与提供者，数据仅在可见时加载。")
        milestone: qsTr("当前没有真实规则数据。")
    }
}
