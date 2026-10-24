pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ScrollView {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    contentWidth: availableWidth
    clip: true
    ColumnLayout {
        width: root.availableWidth
        spacing: 14
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: appearance.implicitHeight + 40
            ColumnLayout {
                id: appearance
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("外观")
                    font.pixelSize: 20
                    font.bold: true
                    color: root.theme.text
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("内置浅色与深色；跟随系统在首帧前解析。")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    spacing: 8
                    Repeater {
                        model: [
                            {
                                "key": "system",
                                "label": qsTr("跟随系统")
                            },
                            {
                                "key": "light",
                                "label": qsTr("浅色")
                            },
                            {
                                "key": "dark",
                                "label": qsTr("深色")
                            }
                        ]
                        delegate: ActionButton {
                            required property var modelData
                            theme: root.theme
                            text: modelData.label
                            prominent: root.ui.themeMode === modelData.key
                            onClicked: root.ui.themeMode = modelData.key
                        }
                    }
                }
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: lifecycle.implicitHeight + 40
            ColumnLayout {
                id: lifecycle
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("窗口与托盘")
                    font.pixelSize: 20
                    font.bold: true
                    color: root.theme.text
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: root.ui.trayAvailable ? qsTr("关闭到托盘；首次关闭需确认。") : qsTr("当前无托盘；关闭时保留窗口并选择最小化或退出。")
                    color: root.theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                CheckBox {
                    font.family: root.theme.fontFamily
                    text: qsTr("隐藏 5 秒后卸载界面（实验选项）")
                    checked: root.ui.unloadWhenHidden
                    enabled: root.ui.trayAvailable
                    onToggled: root.ui.unloadWhenHidden = checked
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("状态 %1 · 展示需求 %2 · 数据流 %3").arg(root.ui.hostState).arg(root.ui.activeDemandCount).arg(root.ui.activeStreamCount)
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: replay.implicitHeight + 40
            ColumnLayout {
                id: replay
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("原型数据")
                    font.pixelSize: 20
                    font.bold: true
                    color: root.theme.text
                }
                CheckBox {
                    font.family: root.theme.fontFamily
                    text: qsTr("启用 500 节点 / %1 连接回放").arg(root.ui.replayConnections)
                    checked: root.ui.replay
                    onToggled: root.ui.replay = checked
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("合成记录仅用于页面与资源验证，不启动内核、不修改系统代理。")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
