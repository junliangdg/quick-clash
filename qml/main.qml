import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ApplicationWindow {
    id: window
    width: 1040
    height: 700
    minimumWidth: 800
    minimumHeight: 540
    visible: true
    title: "QuickClash"
    color: "#f5f5f8"
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: 176
            Layout.fillHeight: true
            color: "#ececf1"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                Label {
                    text: "QuickClash"
                    font.pixelSize: 20
                    font.bold: true
                }
                Label {
                    text: "工程验证 · P0"
                    color: "#656577"
                }
                Item {
                    Layout.fillHeight: true
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 28
            spacing: 20
            Label {
                text: "欢迎使用 QuickClash"
                font.pixelSize: 26
                font.bold: true
            }
            FlatTile {
                Layout.fillWidth: true
                implicitHeight: 160
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    Label {
                        text: "尚未配置"
                        font.pixelSize: 20
                        font.bold: true
                    }
                    Label {
                        text: "P0 工程空壳已加载。订阅、代理与主题将在后续阶段接入。"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        color: "#656577"
                    }
                    Item {
                        Layout.fillHeight: true
                    }
                }
            }
            Item {
                Layout.fillHeight: true
            }
        }
    }
}
