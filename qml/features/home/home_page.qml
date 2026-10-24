pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ScrollView {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    property bool active: ui.uiVisible && visible
    clip: true
    contentWidth: availableWidth
    function syncDemand(): void {
        ui.setDemand("home:traffic", "traffic", active);
    }
    onActiveChanged: syncDemand()
    Component.onCompleted: syncDemand()
    Component.onDestruction: ui.setDemand("home:traffic", "traffic", false)
    ColumnLayout {
        width: root.availableWidth
        spacing: 14
        GridLayout {
            columns: root.availableWidth >= 760 ? 4 : 2
            columnSpacing: 14
            rowSpacing: 14
            Layout.fillWidth: true
            FlatTile {
                theme: root.theme
                Layout.columnSpan: 2
                Layout.fillWidth: true
                implicitHeight: 182
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10
                    Label {
                        font.family: root.theme.fontFamily
                        text: root.ui.replay ? qsTr("回放模式 · 未运行代理") : qsTr("尚未配置")
                        color: root.theme.success
                        font.bold: true
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: root.ui.replay ? qsTr("演示订阅 / 示例节点") : qsTr("从一个订阅开始")
                        color: root.theme.text
                        font.pixelSize: 23
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: qsTr("P1 界面原型 · 官方 Mihomo 将在 P2 接入")
                        color: root.theme.muted
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        ActionButton {
                            theme: root.theme
                            text: qsTr("管理订阅")
                            prominent: true
                            onClicked: root.ui.navigate("subscriptions")
                        }
                        ActionButton {
                            theme: root.theme
                            text: root.ui.replay ? qsTr("结束回放") : qsTr("预览回放")
                            onClicked: root.ui.replay = !root.ui.replay
                        }
                    }
                }
            }
            Repeater {
                model: [
                    {
                        "label": qsTr("上传"),
                        "up": true
                    },
                    {
                        "label": qsTr("下载"),
                        "up": false
                    }
                ]
                delegate: FlatTile {
                    id: rateTile
                    required property var modelData
                    theme: root.theme
                    Layout.fillWidth: true
                    implicitHeight: 182
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        Label {
                            font.family: root.theme.fontFamily
                            text: rateTile.modelData.label
                            color: root.theme.muted
                        }
                        Label {
                            font.family: root.theme.fontFamily
                            text: rateTile.modelData.up ? root.ui.upload : root.ui.download
                            color: root.theme.accent
                            font.pixelSize: 23
                            font.bold: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Label {
                            font.family: root.theme.fontFamily
                            text: root.ui.replay ? qsTr("1 Hz 合成数据") : qsTr("等待内核数据")
                            color: root.theme.muted
                        }
                        Item {
                            Layout.fillHeight: true
                        }
                        TrafficSparkline {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            samples: rateTile.modelData.up ? root.ui.uploadHistory : root.ui.downloadHistory
                            lineColor: root.theme.accent
                            fillColor: root.theme.accentSurface
                        }
                    }
                }
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: controls.childrenRect.height + 28
            Flow {
                id: controls
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("代理模式")
                    color: root.theme.text
                    height: 36
                    verticalAlignment: Text.AlignVCenter
                }
                Repeater {
                    model: [qsTr("规则"), qsTr("全局"), qsTr("直连")]
                    delegate: ActionButton {
                        required property string modelData
                        theme: root.theme
                        text: modelData
                        enabled: false
                    }
                }
                Switch {
                    font.family: root.theme.fontFamily
                    text: qsTr("系统代理")
                    enabled: false
                    palette.windowText: root.theme.muted
                }
                Switch {
                    font.family: root.theme.fontFamily
                    text: qsTr("TUN · P3")
                    enabled: false
                    palette.windowText: root.theme.muted
                }
            }
        }
        GridLayout {
            columns: root.availableWidth >= 650 ? 2 : 1
            columnSpacing: 14
            rowSpacing: 14
            Layout.fillWidth: true
            FlatTile {
                theme: root.theme
                Layout.fillWidth: true
                implicitHeight: 170
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10
                    Label {
                        font.family: root.theme.fontFamily
                        text: qsTr("订阅与网络")
                        font.bold: true
                        color: root.theme.text
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: root.ui.replay ? qsTr("演示订阅 · 用量和到期时间未知") : qsTr("暂无订阅 · 尚未启动内核")
                        color: root.theme.muted
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: qsTr("IP 信息未查询 · 网站测试尚未接入")
                        color: root.theme.muted
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("网站测试")
                        onClicked: root.ui.navigate("tests")
                    }
                }
            }
            EmbeddedHost {
                ui: root.ui
                theme: root.theme
                contentId: "proxy_summary"
                instanceKey: "home:proxy_summary"
                Layout.fillWidth: true
                Layout.preferredHeight: 170
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: info.implicitHeight + 36
            ColumnLayout {
                id: info
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        font.family: root.theme.fontFamily
                        text: qsTr("连接与系统")
                        font.bold: true
                        color: root.theme.text
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("查看连接")
                        onClicked: root.ui.navigate("connections")
                    }
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("首页不订阅完整连接列表。客户端 / 内核内存：尚未采样。")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("活跃展示需求 %1 · 合并数据流 %2").arg(root.ui.activeDemandCount).arg(root.ui.activeStreamCount)
                    color: root.theme.muted
                }
            }
        }
    }
}
