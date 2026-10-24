import QtQuick
import QuickClash.Ui

Item {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    DeferredContent {
        width: parent.width
        theme: root.theme
        heading: qsTr("网站测试尚未接入")
        description: qsTr("P2 提供用户指定的单目标 HTTPS 测试，明确走当前代理且不回退直连。")
        milestone: qsTr("本页不会自动发起 IP 或网站请求。")
    }
}
