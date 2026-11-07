pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ApplicationWindow {
    id: window
    required property UiState ui
    required property font uiFont
    readonly property bool compact: width < 1000
    readonly property bool narrow: width < 600
    property bool expandedRail: false
    property bool collapsedSidebar: false
    readonly property bool wideNavigation: compact ? expandedRail : !collapsedSidebar
    readonly property int navigationWidth: narrow ? 0 : (wideNavigation ? (width >= 1200 ? 208 : 192) : 64)
    readonly property int pageMargin: narrow ? 12 : (compact ? 16 : 24)
    readonly property int pageTopMargin: narrow ? 12 : 16
    readonly property int pageBottomMargin: 12
    // At very narrow sizes, make the existing margin just large enough for the
    // full focus ring and scrollbar; no page width depends on scrollbar visibility.
    readonly property int pageContentMargin: Math.max(pageMargin, tokens.focusMargin + tokens.scrollBarWidth + 2)
    readonly property bool pageReady: pageHost.status === Loader.Ready
    readonly property bool runtimeFailed: ui.runtime.status === "Failed" || ui.runtimeError.length > 0
    readonly property bool runtimeHealthy: !!ui.runtime.ready && !runtimeFailed
    readonly property color runtimeColor: runtimeFailed ? tokens.error : runtimeHealthy ? tokens.success : tokens.muted
    width: 1000
    height: 640
    minimumWidth: 360
    minimumHeight: 480
    visible: false
    title: "QuickClash"
    font: uiFont
    color: tokens.background
    palette.window: tokens.background
    palette.windowText: tokens.text
    palette.base: tokens.surface
    palette.text: tokens.text
    palette.button: tokens.inset
    palette.buttonText: tokens.text
    palette.highlight: tokens.accent
    palette.highlightedText: tokens.accentForeground
    palette.placeholderText: tokens.muted
    palette.mid: tokens.controlOutline
    palette.dark: tokens.muted
    palette.toolTipBase: tokens.surface
    palette.toolTipText: tokens.text
    ThemeTokens { id: tokens; objectName: "theme_tokens"; dark: window.ui.dark; fontFamily: window.uiFont.family; fontPixelSize: window.uiFont.pixelSize }
    onClosing: function(close) { close.accepted = false; ui.requestClose(); }
    Shortcut { sequences: [StandardKey.Close]; onActivated: window.ui.requestClose() }
    Shortcut { sequences: [StandardKey.Quit]; onActivated: window.ui.requestQuit() }
    function syncUserRuleBinding(): void {
        const binding = (window.ui.runtime.features || {}).userRuleBinding || ({});
        if (binding.profileId)
            userRuleBindingLoader.active = true;
        const dialog = userRuleBindingLoader.item as ThemedDialog;
        if (!dialog)
            return;
        // opened is false while transitions run. Always apply the latest intent
        // so a cancelled request cannot leave a dialog open after its enter animation.
        if (binding.profileId)
            dialog.open();
        else
            dialog.close();
    }
    function openUsageNotice(): void {
        usageNoticeLoader.active = true;
        const dialog = usageNoticeLoader.item as UsageNoticeDialog;
        if (dialog) dialog.open();
    }
    Component.onCompleted: {
        syncUserRuleBinding();
        if (ui.usageNoticePending) openUsageNotice();
    }
    Connections {
        target: window.ui
        function onUsageNoticeChanged(): void {
            if (window.ui.usageNoticePending) {
                window.openUsageNotice();
            } else {
                const dialog = usageNoticeLoader.item as UsageNoticeDialog;
                if (dialog && dialog.acknowledgmentRequired) dialog.close();
            }
        }
        function onUsageNoticeRequested(): void { window.openUsageNotice(); }
        function onSettingsQuitConfirmationRequired(): void {
            settingsQuitLoader.active = true;
            const dialog = settingsQuitLoader.item as ThemedDialog;
            if (dialog) dialog.open();
        }
        function onCloseConfirmationRequired(): void {
            closeDialogLoader.active = true;
            const dialog = closeDialogLoader.item as ThemedDialog;
            if (dialog)
                dialog.open();
        }
        function onRuntimeChanged(): void { window.syncUserRuleBinding(); }
    }
    Loader {
        id: usageNoticeLoader
        objectName: "usage_notice_loader"
        active: false
        sourceComponent: UsageNoticeDialog {
            ui: window.ui
            theme: tokens
            parent: Overlay.overlay
            onClosed: usageNoticeLoader.active = false
        }
    }
    Loader {
        id: settingsQuitLoader
        active: false
        sourceComponent: ThemedDialog {
            theme: tokens
            parent: Overlay.overlay
            anchors.centerIn: parent
            width: Math.min(480, parent.width - 32)
            height: Math.min(360, parent.height - 32)
            implicitHeight: height
            objectName: "settings_quit_confirmation"
            title: qsTr("Quit with unapplied settings?")
            modal: true
            standardButtons: Dialog.Ok | Dialog.Cancel
            contentItem: ThemedScrollView {
                theme: tokens
                form: quitSettingsBody
                Label {
                    id: quitSettingsBody
                    text: qsTr("Unsubmitted network and delay target edits will be discarded. Cancel to continue editing.")
                    wrapMode: Text.WordWrap
                    color: tokens.text
                }
            }
            onAccepted: window.ui.discardSettingsAndQuit()
            onClosed: settingsQuitLoader.active = false
        }
    }
    Loader {
        id: userRuleBindingLoader
        active: false
        sourceComponent: ThemedDialog {
            id: userRuleBinding
            objectName: "user_rule_binding_dialog"
            theme: tokens
            parent: window.contentItem
            anchors.centerIn: parent
            width: Math.min(window.width - 32, 440)
            height: Math.min(window.height - 32, 300)
            implicitHeight: height
            modal: true
            title: qsTr("Choose a proxy group")
            standardButtons: Dialog.Ok | Dialog.Cancel
            readonly property var binding: (window.ui.runtime.features || {}).userRuleBinding || ({})
            onOpened: {
                standardButton(Dialog.Ok).enabled = Qt.binding(function() {
                    return bindingGroup.currentIndex > 0;
                });
            }
            onAccepted: window.ui.runtimeCommand("user_rules_bind", [bindingGroup.currentText])
            onRejected: window.ui.runtimeCommand("user_rules_cancel_binding")
            contentItem: ColumnLayout {
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: tokens.text
                    text: qsTr("Global proxy rules need a group for %1.").arg(userRuleBinding.binding.profileName || "")
                }
                ThemedComboBox {
                    id: bindingGroup
                    objectName: "user_rule_binding_group"
                    theme: tokens
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Proxy group")
                    model: [qsTr("Choose a group")].concat(userRuleBinding.binding.groups || [])
                }
                Label {
                    visible: bindingGroup.count <= 1
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: tokens.warning
                    text: qsTr("This profile has no proxy groups. Choose another profile or add a group first.")
                }
            }
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
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 16
                RowLayout {
                    Layout.fillWidth: window.wideNavigation
                    Layout.alignment: window.wideNavigation ? Qt.AlignLeft : Qt.AlignHCenter
                    Layout.topMargin: 8
                    spacing: 10
                    Image {
                        objectName: "brand_mark"
                        visible: window.wideNavigation
                        source: tokens.dark ? "qrc:/icons/brand/qcat_tray_light.svg" : "qrc:/icons/brand/qcat_tray_dark.svg"
                        Layout.preferredWidth: tokens.scaledSize(24)
                        Layout.preferredHeight: tokens.scaledSize(24)
                        sourceSize.width: width
                        sourceSize.height: height
                        Accessible.ignored: true
                    }
                    Label {
                        visible: window.wideNavigation
                        Layout.fillWidth: true
                        text: "QuickClash"
                        font.pixelSize: tokens.scaledSize(17)
                        font.weight: Font.DemiBold
                        color: tokens.text
                        elide: Text.ElideRight
                    }
                    ToolButton {
                        id: navigationToggle
                        objectName: "navigation_toggle"
                        Layout.preferredWidth: tokens.scaledSize(32)
                        Layout.preferredHeight: tokens.scaledSize(36)
                        text: window.wideNavigation ? qsTr("Collapse navigation") : qsTr("Expand navigation")
                        display: AbstractButton.IconOnly
                        icon.source: window.wideNavigation ? "qrc:/icons/navigation/panel_left_close.svg" : "qrc:/icons/navigation/panel_left_open.svg"
                        icon.width: tokens.scaledSize(20)
                        icon.height: tokens.scaledSize(20)
                        icon.color: tokens.muted
                        activeFocusOnTab: true
                        Accessible.name: text
                        onClicked: {
                            if (window.compact)
                                window.expandedRail = !window.expandedRail;
                            else
                                window.collapsedSidebar = !window.collapsedSidebar;
                        }
                        ThemedToolTip { theme: tokens; visible: navigationToggle.hovered; text: navigationToggle.text }
                        background: StateSurface { theme: tokens; hovered: navigationToggle.hovered; down: navigationToggle.down; focused: navigationToggle.activeFocus }
                    }
                }
                ListView {
                    id: navigation
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 4
                    clip: true
                    model: window.ui.navigation.filter(entry => entry.pageId !== "settings")
                    delegate: navigationDelegate
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: tokens.rowHeight
                    interactive: false
                    model: window.ui.navigation.filter(entry => entry.pageId === "settings")
                    delegate: navigationDelegate
                }
            }
        }
        Component {
            id: navigationDelegate
            ItemDelegate {
                id: entry
                required property var modelData
                objectName: "navigation_" + modelData.pageId
                width: ListView.view.width
                height: tokens.rowHeight
                padding: 10
                spacing: 16
                text: modelData.title
                display: window.wideNavigation ? AbstractButton.TextBesideIcon : AbstractButton.IconOnly
                icon.source: modelData.icon
                icon.width: tokens.scaledSize(20)
                icon.height: tokens.scaledSize(20)
                icon.color: highlighted ? tokens.onSelection : tokens.muted
                palette.text: highlighted ? tokens.onSelection : tokens.text
                palette.highlightedText: tokens.onSelection
                font.family: tokens.fontFamily
                font.pixelSize: tokens.navigationSize
                font.weight: highlighted ? Font.Bold : Font.Medium
                highlighted: window.ui.currentPage === modelData.pageId
                activeFocusOnTab: true
                Accessible.name: modelData.title
                onClicked: window.ui.navigate(modelData.pageId)
                ThemedToolTip { theme: tokens; visible: entry.hovered && !window.wideNavigation; text: entry.modelData.title }
                background: StateSurface { theme: tokens; selected: entry.highlighted; hovered: entry.hovered; down: entry.down; focused: entry.activeFocus; insetFocus: true }
            }
        }
        Item {
            id: mainArea
            anchors.left: sideNavigation.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: bottomNavigation.top
            ColumnLayout {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: window.pageTopMargin
                anchors.bottomMargin: window.pageBottomMargin
                width: Math.min(parent.width - 2 * window.pageContentMargin, 1440)
                spacing: 8
                GridLayout {
                    id: pageHeading
                    objectName: "page_title_row"
                    readonly property bool stackedStatus: window.narrow && width < pageTitle.implicitWidth + runtimeBadgeContent.implicitWidth + 18 + (backButton.visible ? backButton.implicitWidth + columnSpacing : morePagesButton.visible ? morePagesButton.implicitWidth + columnSpacing : 0) + columnSpacing
                    columns: stackedStatus ? 2 : backButton.visible || morePagesButton.visible ? 3 : 2
                    columnSpacing: 8
                    rowSpacing: 4
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(tokens.controlHeight, implicitHeight)
                    ActionButton {
                        id: backButton
                        theme: tokens
                        Layout.row: 0
                        Layout.column: 0
                        visible: window.narrow && !["home", "proxies", "subscriptions", "settings"].includes(window.ui.currentPage)
                        text: "←"
                        ghost: true
                        Accessible.name: qsTr("Back to settings")
                        onClicked: window.ui.navigate("settings")
                    }
                    Label {
                        id: pageTitle
                        Layout.row: 0
                        Layout.column: backButton.visible ? 1 : 0
                        Layout.columnSpan: pageHeading.stackedStatus && !backButton.visible && !morePagesButton.visible ? 2 : 1
                        text: window.ui.pageTitle
                        font.pixelSize: window.narrow ? tokens.scaledSize(22) : tokens.titleSize
                        font.weight: Font.DemiBold
                        color: tokens.text
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        elide: Text.ElideRight
                    }
                    ActionButton {
                        id: morePagesButton
                        theme: tokens
                        Layout.row: 0
                        Layout.column: 1
                        objectName: "settings_more_pages"
                        visible: window.narrow && window.ui.currentPage === "settings"
                        text: qsTr("Other")
                        Accessible.name: qsTr("More pages")
                        selected: settingsPagesMenu.visible
                        onClicked: settingsPagesMenu.open()
                        Menu {
                            id: settingsPagesMenu
                            objectName: "settings_pages_menu"
                            y: morePagesButton.height + 6
                            margins: window.pageMargin
                            padding: 6
                            width: Math.min(Math.max(200, connectionsMenuItem.implicitWidth + leftPadding + rightPadding), window.width - 2 * window.pageMargin)
                            palette.window: tokens.surface
                            palette.windowText: tokens.text
                            palette.light: tokens.hover
                            palette.midlight: tokens.pressed
                            palette.dark: tokens.controlOutline
                            background: Rectangle {
                                implicitWidth: 200
                                implicitHeight: 40
                                radius: tokens.controlRadius
                                color: tokens.surface
                                border.width: 1
                                border.color: tokens.border
                            }
                            component PageMenuItem: MenuItem {
                                id: entry
                                palette.windowText: highlighted ? tokens.onSelection : tokens.text
                                background: StateSurface {
                                    theme: tokens
                                    implicitWidth: 200
                                    implicitHeight: 40
                                    selected: entry.highlighted
                                    hovered: entry.hovered
                                    down: entry.down
                                }
                            }
                            onClosed: {
                                if (morePagesButton.visible)
                                    morePagesButton.forceActiveFocus(Qt.PopupFocusReason);
                            }
                            PageMenuItem { id: connectionsMenuItem; objectName: "settings_page_connections"; text: qsTranslate("FeatureRegistry", "Connections"); onTriggered: window.ui.navigate("connections") }
                            PageMenuItem { objectName: "settings_page_rules"; text: qsTranslate("FeatureRegistry", "Rules"); onTriggered: window.ui.navigate("rules") }
                            PageMenuItem { objectName: "settings_page_logs"; text: qsTranslate("FeatureRegistry", "Logs"); onTriggered: window.ui.navigate("logs") }
                            PageMenuItem { objectName: "settings_page_tests"; text: qsTranslate("FeatureRegistry", "Tests"); onTriggered: window.ui.navigate("tests") }
                        }
                    }
                    Rectangle {
                        id: runtimeStatusBadge
                        objectName: "runtime_status_badge"
                        Layout.row: pageHeading.stackedStatus ? 1 : 0
                        Layout.column: pageHeading.stackedStatus ? 0 : backButton.visible || morePagesButton.visible ? 2 : 1
                        Layout.columnSpan: pageHeading.stackedStatus ? 2 : 1
                        Layout.alignment: Qt.AlignRight
                        Layout.preferredWidth: runtimeBadgeContent.implicitWidth + 18
                        Layout.preferredHeight: Math.max(28, runtimeBadgeContent.implicitHeight + 8)
                        radius: 14
                        color: window.runtimeFailed ? tokens.errorSurface : window.runtimeHealthy ? tokens.successSurface : tokens.inset
                        Accessible.role: Accessible.StaticText
                        Accessible.name: runtimeBadgeText.text
                        Accessible.description: window.ui.runtimeError
                        activeFocusOnTab: true
                        border.width: activeFocus ? tokens.focusWidth : 0
                        border.color: tokens.accent
                        RowLayout {
                            id: runtimeBadgeContent
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle { implicitWidth: 6; implicitHeight: 6; radius: 3; color: window.runtimeColor }
                            Label {
                                id: runtimeBadgeText
                                objectName: "runtime_status_text"
                                text: window.runtimeFailed ? qsTr("Runtime error") : window.runtimeHealthy ? qsTr("Core ready") : window.ui.runtimeStatus
                                color: window.runtimeColor
                                font.pixelSize: tokens.supportingSize
                            }
                        }
                        HoverHandler { id: runtimeBadgeHover }
                        ThemedToolTip {
                            objectName: "runtime_status_tooltip"
                            theme: tokens
                            visible: (runtimeBadgeHover.hovered || runtimeStatusBadge.activeFocus) && window.ui.runtimeError.length > 0
                            text: window.ui.runtimeError
                        }
                    }
                }
                Label {
                    visible: window.ui.routeError.length > 0
                    text: window.ui.routeError
                    color: tokens.error
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 0
                    Layout.minimumHeight: 0
                    PageHost {
                        id: pageHost
                        objectName: "active_page_host"
                        ui: window.ui
                        theme: tokens
                        anchors.fill: parent
                        anchors.leftMargin: -pageHost.pageLeftOutset
                        anchors.rightMargin: -pageHost.pageRightOutset
                    }
                }
            }
        }
        Rectangle {
            id: bottomNavigation
            objectName: "bottom_navigation"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: window.narrow
            height: visible ? tokens.scaledSize(56) : 0
            color: tokens.sidebar
            Row {
                id: bottomRow
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6
                Repeater {
                    id: bottomEntries
                    model: window.ui.compactNavigation
                    delegate: ItemDelegate {
                        id: bottomEntry
                        required property var modelData
                        objectName: "bottom_navigation_" + modelData.pageId
                        width: (bottomRow.width - bottomRow.spacing * (bottomEntries.count - 1)) / Math.max(1, bottomEntries.count)
                        height: bottomRow.height
                        implicitWidth: 0
                        text: modelData.title
                        display: AbstractButton.IconOnly
                        icon.source: modelData.icon
                        icon.width: tokens.scaledSize(20)
                        icon.height: tokens.scaledSize(20)
                        icon.color: highlighted ? tokens.onSelection : tokens.muted
                        highlighted: window.ui.currentPage === modelData.pageId
                        activeFocusOnTab: true
                        Accessible.name: modelData.title
                        ThemedToolTip { theme: tokens; visible: bottomEntry.hovered; text: bottomEntry.modelData.title }
                        onClicked: window.ui.navigate(modelData.pageId)
                        background: StateSurface { theme: tokens; selected: bottomEntry.highlighted; hovered: bottomEntry.hovered; down: bottomEntry.down; focused: bottomEntry.activeFocus; insetFocus: true }
                    }
                }
            }
        }
    }
    Loader {
        id: closeDialogLoader
        active: false
        sourceComponent: ThemedDialog {
            id: closeDialog
            theme: tokens
            objectName: "close_confirmation"
            parent: window.contentItem
            anchors.centerIn: parent
            width: Math.min(window.width - 40, 460)
            title: window.ui.trayAvailable ? qsTr("Close to tray") : qsTr("Tray unavailable")
            modal: true
            closePolicy: Popup.CloseOnEscape
            initialFocusItem: closeCancel
            contentItem: ColumnLayout {
                spacing: tokens.gap
                Label {
                    text: window.ui.trayAvailable ? qsTr("QuickClash will keep running. Open it again from the tray. This choice will be remembered.") : qsTr("The window will remain visible. You can minimize it or quit QuickClash.")
                    color: tokens.text
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    ActionButton { id: closeCancel; objectName: "close_cancel"; theme: tokens; text: qsTr("Cancel"); onClicked: closeDialog.close() }
                    Item { Layout.fillWidth: true }
                    ActionButton { theme: tokens; text: qsTr("Quit"); destructive: true; onClicked: { closeDialog.close(); window.ui.requestQuit(); } }
                    ActionButton {
                        theme: tokens
                        prominent: true
                        text: window.ui.trayAvailable ? qsTr("Close to tray") : qsTr("Minimize")
                        onClicked: { closeDialog.close(); if (window.ui.trayAvailable) window.ui.requestClose(true); else window.ui.requestMinimize(); }
                    }
                }
            }
        }
    }
}
