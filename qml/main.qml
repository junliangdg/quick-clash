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
    readonly property bool narrow: width < 600
    property bool expandedRail: false
    property bool wideNavigation: !compact || expandedRail
    readonly property int navigationWidth: narrow ? 0 : (wideNavigation ? (width >= 1200 ? 208 : 192) : 64)
    readonly property int pageMargin: narrow ? 12 : (compact ? 16 : 24)
    readonly property bool pageReady: pageHost.status === Loader.Ready
    width: 1160
    height: 780
    minimumWidth: 360
    minimumHeight: 480
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
    Item {
        anchors.fill: parent
        Rectangle {
            id: sideNavigation
            objectName: "side_navigation"
            visible: !window.narrow
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: window.navigationWidth
            clip: true
            color: tokens.sidebar
            Item {
                anchors.fill: parent
                anchors.margins: 10
                Item {
                    id: navigationHeader
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 42
                    ActionButton {
                        theme: tokens
                        text: "Q"
                        width: 42
                        height: 42
                        onClicked: window.expandedRail = !window.expandedRail
                        Accessible.name: qsTr("Expand or collapse navigation")
                    }
                    Label {
                        x: 54
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, parent.width - x)
                        font.family: tokens.fontFamily
                        text: "QuickClash"
                        visible: window.wideNavigation
                        elide: Text.ElideRight
                        font.bold: true
                        font.pixelSize: 18
                        color: tokens.text
                    }
                }
                ListView {
                    id: navigation
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: navigationHeader.bottom
                    anchors.topMargin: 16
                    anchors.bottom: navigationVersion.top
                    anchors.bottomMargin: 16
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
                        implicitWidth: 44
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
                        contentItem: Item {
                            Label {
                                width: 24
                                height: parent.height
                                font.family: tokens.fontFamily
                                text: entry.modelData.glyph
                                color: entry.highlighted ? tokens.accent : tokens.muted
                                font.pixelSize: 22
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            Label {
                                x: 36
                                width: Math.max(0, parent.width - x)
                                height: parent.height
                                font.family: tokens.fontFamily
                                visible: window.wideNavigation
                                text: entry.modelData.title
                                color: entry.highlighted ? tokens.accent : tokens.text
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
                Label {
                    id: navigationVersion
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 18
                    font.family: tokens.fontFamily
                    visible: window.wideNavigation
                    text: "QuickClash · 0.1"
                    color: tokens.muted
                    font.pixelSize: 12
                }
            }
        }
        ColumnLayout {
            anchors.left: sideNavigation.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: bottomNavigation.top
            anchors.margins: window.pageMargin
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                Label {
                    font.family: tokens.fontFamily
                    text: window.ui.pageTitle
                    font.pixelSize: window.narrow ? 22 : 26
                    font.bold: true
                    color: tokens.text
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                }
                Label {
                    font.family: tokens.fontFamily
                    visible: !window.narrow
                    text: window.ui.replay ? qsTr("Replay data") : window.ui.runtimeStatus
                    color: tokens.muted
                }
                ActionButton {
                    theme: tokens
                    text: window.ui.dark ? qsTr("Light") : qsTr("Dark")
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
                objectName: "active_page_host"
                ui: window.ui
                theme: tokens
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                Layout.minimumHeight: 0
            }
        }
        Rectangle {
            id: bottomNavigation
            objectName: "bottom_navigation"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: window.narrow
            height: visible ? 68 : 0
            color: tokens.sidebar
            Row {
                id: bottomRow
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6
                Repeater {
                    model: window.ui.compactNavigation
                    delegate: ItemDelegate {
                        id: bottomEntry
                        required property var modelData
                        objectName: "bottom_navigation_" + modelData.pageId
                        width: (bottomRow.width - bottomRow.spacing * 2) / 3
                        height: bottomRow.height
                        implicitWidth: 0
                        highlighted: window.ui.currentPage === modelData.pageId
                        activeFocusOnTab: true
                        Accessible.name: modelData.title
                        onClicked: window.ui.navigate(modelData.pageId)
                        background: Rectangle {
                            radius: 12
                            color: bottomEntry.highlighted || bottomEntry.hovered ? tokens.accentSurface : "transparent"
                            border.width: bottomEntry.activeFocus ? 1 : 0
                            border.color: tokens.accent
                        }
                        contentItem: Item {
                            Label {
                                anchors.top: parent.top
                                anchors.horizontalCenter: parent.horizontalCenter
                                height: 26
                                text: bottomEntry.modelData.glyph
                                color: bottomEntry.highlighted ? tokens.accent : tokens.muted
                                font.pixelSize: 20
                                verticalAlignment: Text.AlignVCenter
                            }
                            Label {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 18
                                text: bottomEntry.modelData.title
                                color: bottomEntry.highlighted ? tokens.accent : tokens.text
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
    Dialog {
        id: closeDialog
        objectName: "close_confirmation"
        parent: window.contentItem
        anchors.centerIn: parent
        width: Math.min(window.width - 40, 460)
        title: window.ui.trayAvailable ? qsTr("Close to tray") : qsTr("Tray unavailable")
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
                text: window.ui.trayAvailable ? qsTr("QuickClash will keep running. Open it again from the tray. This choice will be remembered.") : qsTr("The window will remain visible. You can minimize it or quit QuickClash.")
                color: tokens.text
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                ActionButton {
                    theme: tokens
                    text: qsTr("Cancel")
                    onClicked: closeDialog.close()
                }
                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    theme: tokens
                    text: qsTr("Quit")
                    onClicked: {
                        closeDialog.close();
                        window.ui.requestQuit();
                    }
                }
                ActionButton {
                    theme: tokens
                    prominent: true
                    text: window.ui.trayAvailable ? qsTr("Close to tray") : qsTr("Minimize")
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
