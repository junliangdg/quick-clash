import QtQuick
import QuickClash.Ui

Item {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    DeferredContent {
        width: parent.width
        theme: root.theme
        heading: qsTr("日志尚未接入")
        description: qsTr("P2 提供有界错误与修复入口，P3 扩展实时日志、筛选与脱敏导出。")
        milestone: qsTr("当前没有伪造的运行日志。")
    }
}
