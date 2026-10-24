import QtQuick
import QuickClash.Ui

Item {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    DeferredContent {
        width: parent.width
        theme: root.theme
        heading: qsTr("订阅尚未接入")
        description: qsTr("P2 将支持 HTTPS 订阅、本地文件导入、候选校验、更新与配置切换。")
        milestone: qsTr("当前不能添加订阅或启动代理。")
    }
}
