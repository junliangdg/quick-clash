pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ApplicationWindow {
    id: window
    required property UiState ui
    required property string uiFontFamily
    property bool compact: width < 1000
    property bool expandedRail: false
    property bool wideNavigation: !compact || expandedRail
    readonly property bool pageReady: pageHost.status === Loader.Ready
    width: 1160
    height: 780
    minimumWidth: 720
    minimumHeight: 560
    visible: false
    title: "QuickClash"
    font.family: uiFontFamily
    color: tokens.background
    palette.window: tokens.background
    palette.windowText: tokens.text
    palette.base: tokens.surface
    palette.text: tokens.text
    palette.button: tokens.inset
    palette.buttonText: tokens.text
    palette.highlight: tokens.accent
    palette.highlightedText: "#ffffff"
    palette.mid: tokens.border
    ThemeTokens {
        id: tokens
        dark: window.ui.dark
        fontFamily: window.uiFontFamily
    }
    onClosing: function (close) {
        close.accepted = false;
        ui.requestClose();
    }
    Shortcut {
        sequences: [StandardKey.Close]
        onActivated: window.ui.requestClose()
    }
    Shortcut {
        sequences: [StandardKey.Quit]
        onActivated: window.ui.requestQuit()
    }
    Connections {
        target: window.ui
        function onCloseConfirmationRequired(): void {
            closeDialog.open();
        }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: window.wideNavigation ? (window.width >= 1200 ? 208 : 192) : 64
            Layout.fillHeight: true
            color: tokens.sidebar
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 16
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton {
                        theme: tokens
                        text: "Q"
                        implicitWidth: 42
                        onClicked: window.expandedRail = !window.expandedRail
                        Accessible.name: qsTr("展开或收起导航")
                    }
                    Label {
                        font.family: tokens.fontFamily
                        text: "QuickClash"
                        visible: window.wideNavigation
                        font.bold: true
                        font.pixelSize: 18
                        color: tokens.text
                    }
                }
                ListView {
                    id: navigation
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 4
                    clip: true
                    model: window.ui.navigation
                    delegate: ItemDelegate {
                        id: entry
                        font.family: tokens.fontFamily
                        required property var modelData
                        objectName: "navigation_" + modelData.pageId
                        width: ListView.view.width
                        height: 44
                        highlighted: window.ui.currentPage === modelData.pageId
                        activeFocusOnTab: true
                        Accessible.name: modelData.title
                        ToolTip.visible: hovered && !window.wideNavigation
                        ToolTip.text: modelData.title
                        onClicked: window.ui.navigate(modelData.pageId)
                        background: Rectangle {
                            radius: 11
                            color: entry.highlighted || entry.hovered ? tokens.accentSurface : "transparent"
                            border.width: entry.activeFocus ? 1 : 0
                            border.color: tokens.accent
                        }
                        contentItem: RowLayout {
                            spacing: 12
                            Label {
                                font.family: tokens.fontFamily
                                text: entry.modelData.glyph
                                color: entry.highlighted ? tokens.accent : tokens.muted
                                font.pixelSize: 22
                                Layout.preferredWidth: 24
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Label {
                                font.family: tokens.fontFamily
                                visible: window.wideNavigation
                                text: entry.modelData.title
                                color: entry.highlighted ? tokens.accent : tokens.text
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
                Label {
                    font.family: tokens.fontFamily
                    visible: window.wideNavigation
                    text: qsTr("P1 · 界面原型")
                    color: tokens.muted
                    font.pixelSize: 12
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: window.compact ? 16 : 24
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                Label {
                    font.family: tokens.fontFamily
                    text: window.ui.pageTitle
                    font.pixelSize: 26
                    font.bold: true
                    color: tokens.text
                    Layout.fillWidth: true
                }
                Label {
                    font.family: tokens.fontFamily
                    text: window.ui.replay ? qsTr("回放数据") : qsTr("内核未接入")
                    color: tokens.muted
                }
                ActionButton {
                    theme: tokens
                    text: window.ui.dark ? qsTr("浅色") : qsTr("深色")
                    onClicked: window.ui.themeMode = window.ui.dark ? "light" : "dark"
                }
            }
            Label {
                font.family: tokens.fontFamily
                visible: window.ui.routeError.length > 0
                text: window.ui.routeError
                color: tokens.warning
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            PageHost {
                id: pageHost
                ui: window.ui
                theme: tokens
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }
    Dialog {
        id: closeDialog
        objectName: "close_confirmation"
        parent: window.contentItem
        anchors.centerIn: parent
        width: Math.min(window.width - 40, 460)
        title: window.ui.trayAvailable ? qsTr("关闭到托盘") : qsTr("当前没有可用托盘")
        modal: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            radius: 16
            color: tokens.surface
            border.width: 1
            border.color: tokens.border
        }
        contentItem: ColumnLayout {
            spacing: 16
            Label {
                font.family: tokens.fontFamily
                text: window.ui.trayAvailable ? qsTr("关闭后 QuickClash 继续驻留，可从托盘重新打开。确认后记住此选择。") : qsTr("窗口将保持可见。你可以最小化，或明确退出 QuickClash。")
                color: tokens.text
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                ActionButton {
                    theme: tokens
                    text: qsTr("取消")
                    onClicked: closeDialog.close()
                }
                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    theme: tokens
                    text: qsTr("退出")
                    onClicked: {
                        closeDialog.close();
                        window.ui.requestQuit();
                    }
                }
                ActionButton {
                    theme: tokens
                    prominent: true
                    text: window.ui.trayAvailable ? qsTr("关闭到托盘") : qsTr("最小化")
                    onClicked: {
                        closeDialog.close();
                        if (window.ui.trayAvailable)
                            window.ui.requestClose(true);
                        else
                            window.ui.requestMinimize();
                    }
                }
            }
        }
    }
}
