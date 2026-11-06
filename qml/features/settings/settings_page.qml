pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QuickClash.Ui

Item {
    id: root
    objectName: "settings_page"
    required property UiState ui
    required property ThemeTokens theme
    readonly property int category: ui.settingsSession.category || 0
    property int loadedCategory: 0
    property bool viewReady: false
    property bool restoring: true
    property bool restoreFieldFocus: true
    property bool recoveryRequested: false
    readonly property real availableWidth: width
    readonly property real pageLeftOutset: theme.focusMargin
    readonly property real pageRightOutset: theme.focusMargin + theme.scrollBarWidth
    readonly property Item form: settingsForm
    readonly property Flickable viewport: body.contentItem as Flickable
    function rememberView(): void {
        if (!viewReady || !viewport || !categoryLoader)
            return;
        const owner = root.Window.window;
        const focus = owner && owner.activeFocusItem ? owner.activeFocusItem : null;
        let ancestor = focus;
        while (ancestor && ancestor !== categoryLoader.item)
            ancestor = ancestor.parent;
        root.ui.rememberSettingsView(loadedCategory, viewport.contentY, ancestor && focus.objectName ? focus.objectName : root.ui.settingsFocus(loadedCategory));
    }
    function restoreFocus(item, name): bool {
        if (item.objectName === name && item.enabled && item.visible) {
            item.forceActiveFocus(Qt.OtherFocusReason);
            return true;
        }
        for (const child of item.children)
            if (restoreFocus(child, name))
                return true;
        return false;
    }
    function revealFocusedField(): void {
        if (restoring || !viewport || !categoryLoader.item)
            return;
        const owner = root.Window.window;
        const focus = owner ? owner.activeFocusItem : null;
        let ancestor = focus;
        while (ancestor && ancestor !== categoryLoader.item)
            ancestor = ancestor.parent;
        if (!focus || !ancestor)
            return;
        const bounds = focus.mapToItem(viewport.contentItem, 0, 0, focus.width, focus.height);
        const margin = root.theme.focusMargin;
        const maximum = Math.max(0, body.contentHeight - body.availableHeight);
        if (bounds.y < viewport.contentY + margin)
            viewport.contentY = Math.max(0, bounds.y - margin);
        else if (bounds.y + bounds.height > viewport.contentY + body.availableHeight - margin)
            viewport.contentY = Math.min(maximum, bounds.y + bounds.height - body.availableHeight + margin);
        root.rememberView();
    }
    function polishTree(item): void {
        // Nested Controls and Layouts can all defer their implicit sizes to polish.
        for (const child of item.children)
            polishTree(child);
        item.ensurePolished();
    }
    function revealRecovery(): void {
        const data = categoryLoader.item as SettingsData;
        if (!recoveryRequested || restoring || !viewport || !data || !data.recoveryAnchor.visible)
            return;
        polishTree(settingsForm);
        polishTree(settingsForm);
        const y = data.recoveryAnchor.mapToItem(viewport.contentItem, 0, 0).y;
        // Do not consume the request while nested layouts still have their old height.
        if (body.contentHeight + 1 < y + data.recoveryAnchor.height)
            return;
        viewport.contentY = Math.max(0, Math.min(y - theme.focusMargin, body.contentHeight - body.availableHeight));
        recoveryRequested = false;
    }
    onWidthChanged: Qt.callLater(revealFocusedField)
    onHeightChanged: Qt.callLater(revealFocusedField)
    onCategoryChanged: {
        if (category !== 3)
            recoveryRequested = false;
        rememberView();
        restoring = true;
        restoreFieldFocus = false;
        loadedCategory = category;
    }
    Component.onCompleted: {
        loadedCategory = category;
        viewReady = true;
    }
    Component.onDestruction: rememberView()
    ColumnLayout {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.min(1040, root.width)
        spacing: 4
        SettingsTabs {
            theme: root.theme
            ui: root.ui
            Layout.fillWidth: true
        }
        ThemedScrollView {
            id: body
            objectName: "settings_body"
            theme: root.theme
            form: settingsForm
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            ColumnLayout {
                id: settingsForm
                spacing: root.theme.gap
                Label {
                    id: storageWarning
                    objectName: "settings_storage_warning"
                    visible: root.ui.storageError.length > 0 || !!root.ui.runtime.readOnly
                    text: qsTr("Settings are read-only or could not be saved. Existing data is preserved; recovery is available under Data.")
                    color: root.theme.warning
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                SettingsAction {
                    theme: root.theme
                    objectName: "settings_open_recovery"
                    visible: storageWarning.visible && root.category !== 3
                    text: qsTr("Open data and recovery")
                    Layout.maximumWidth: parent.width
                    onClicked: {
                        root.ui.setSettingsCategory(3);
                        root.recoveryRequested = true;
                        if (!root.restoring)
                            recoveryLater.restart();
                    }
                }
                Loader {
                    id: categoryLoader
                    objectName: "settings_category_loader"
                    Layout.fillWidth: true
                    sourceComponent: [generalCategory, networkCategory, resourcesCategory, dataCategory, aboutCategory][root.loadedCategory]
                    onLoaded: restoreLater.restart()
                    Timer {
                        id: restoreLater
                        interval: 0
                        onTriggered: {
                            if (!categoryLoader.item || !root.viewport)
                                return;
                            // Clamp the remembered position against this category's
                            // layout, not the previous Loader item's deferred height.
                            root.polishTree(settingsForm);
                            root.polishTree(settingsForm);
                            root.viewport.contentY = Math.min(root.ui.settingsPosition(root.category), Math.max(0, body.contentHeight - body.availableHeight));
                            root.restoring = false;
                            if (root.restoreFieldFocus) {
                                const name = root.ui.settingsFocus(root.category);
                                if (name)
                                    root.restoreFocus(categoryLoader.item, name);
                                root.restoreFieldFocus = false;
                            }
                            if (root.recoveryRequested)
                                recoveryLater.restart();
                        }
                    }
                }
            }
            Timer {
                id: recoveryLater
                interval: 0
                onTriggered: root.revealRecovery()
            }
            Connections {
                target: root.viewport
                function onContentYChanged(): void {
                    if (!root.restoring)
                        root.rememberView();
                }
            }
            Connections {
                target: body
                enabled: root.recoveryRequested && !root.restoring
                function onContentHeightChanged(): void {
                    recoveryLater.restart();
                }
            }
            Connections {
                target: root.Window.window
                function onActiveFocusItemChanged(): void {
                    root.revealFocusedField();
                }
            }
            Connections {
                target: root.Window.window
                enabled: root.recoveryRequested
                function onFrameSwapped(): void {
                    root.revealRecovery();
                }
            }
        }
    }
    Component {
        id: generalCategory
        SettingsGeneral {
            ui: root.ui
            theme: root.theme
        }
    }
    Component {
        id: networkCategory
        SettingsNetwork {
            ui: root.ui
            theme: root.theme
        }
    }
    Component {
        id: resourcesCategory
        SettingsResources {
            ui: root.ui
            theme: root.theme
        }
    }
    Component {
        id: dataCategory
        SettingsData {
            ui: root.ui
            theme: root.theme
        }
    }
    Component {
        id: aboutCategory
        SettingsAbout {
            ui: root.ui
            theme: root.theme
        }
    }
}
