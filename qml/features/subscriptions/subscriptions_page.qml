pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QuickClash.Ui

ListView {
    id: root
    objectName: "subscriptions_page"
    required property UiState ui
    required property ThemeTokens theme
    property string managedId: ""
    property var managedProfile: ({})
    property string urlRequestToken: ""
    property string urlRequestAction: ""
    property string urlResult: ""
    function editProfile(profileId, layer): void {
        editorLoader.active = true;
        const editor = editorLoader.item as ProfileEditor;
        if (editor)
            editor.edit(profileId, layer);
    }
    function beginImport(command, arguments) {
        importDialog.profileCount = ui.profileModel.count;
        importDialog.attempted = true;
        ui.runtimeCommand(command, arguments);
    }
    function finishImportIfSaved() {
        if (!importDialog.visible || !importDialog.attempted || ui.runtime.busy || ui.runtime.error)
            return;
        if (importDialog.saved)
            importDialog.close();
    }
    function clearSavedUrl() {
        urlRequestToken = "";
        urlRequestAction = "";
        urlResult = "";
        savedUrl.clear();
    }
    function requestSavedUrl(action) {
        clearSavedUrl();
        urlRequestAction = action;
        urlRequestToken = Date.now().toString() + ":" + Math.random().toString();
        ui.runtimeCommand("subscription_url", [managedId, urlRequestToken]);
    }
    property int historyIndex: 0
    property string historyBase: ""
    readonly property var downloadRoutes: ["Direct", "RunningProxy", "Auto"]
    readonly property var downloadRouteLabels: [qsTr("Direct download"), qsTr("Running proxy"), qsTr("Auto: direct, then running proxy")]
    // Keep the list's scrollbar in the shell margin, outside the shared content width.
    readonly property real pageRightOutset: theme.scrollBarWidth
    readonly property real availableWidth: Math.max(0, width - theme.scrollBarWidth)
    readonly property int cardColumns: availableWidth >= 900 ? 3 : availableWidth >= 580 ? 2 : 1
    function scrollToRow(row) {
        positionViewAtIndex(row, ListView.Beginning);
    }
    function focusRow(row) {
        if (count === 0)
            return;
        currentIndex = Math.max(0, Math.min(count - 1, row));
        positionViewAtIndex(currentIndex, ListView.Contain);
        Qt.callLater(function () {
            if (root.currentItem)
                root.currentItem.focusFirst();
        });
    }
    Keys.onDownPressed: focusRow(currentIndex + 1)
    Keys.onUpPressed: focusRow(currentIndex - 1)
    function refreshManaged() {
        const previousName = managedProfile.name || "";
        const previousSource = managedProfile.secretRef || "";
        managedProfile = ui.profileModel.profile(managedId);
        if ((managedProfile.secretRef || "") !== previousSource)
            clearSavedUrl();
        if (!managedProfile.id) {
            rollbackConfirmation.close();
            deleteConfirmation.close();
            management.close();
            managedId = "";
        } else if (itemName.text === previousName || !itemName.activeFocus) {
            itemName.text = managedProfile.name || "";
        }
    }
    function manage(profileId) {
        clearSavedUrl();
        managedId = profileId;
        refreshManaged();
        if (managedId)
            management.open();
    }
    Connections {
        target: root.ui.profileModel
        function onProfileChanged(profileId: string) {
            if (profileId === root.managedId)
                root.refreshManaged();
        }
    }
    Connections {
        target: root.ui
        function onUiVisibleChanged() {
            if (!root.ui.uiVisible) {
                root.clearSavedUrl();
                importDialog.close();
            }
        }
        function onRuntimeChanged() {
            root.finishImportIfSaved();
        }
        function onProfilesChanged() {
            root.finishImportIfSaved();
        }
        function onSubscriptionUrlReady(profileId: string, token: string, url: string) {
            if (!management.visible || !root.visible || !root.ui.uiVisible || profileId !== root.managedId || token !== root.urlRequestToken || !token)
                return;
            const action = root.urlRequestAction;
            root.urlRequestToken = "";
            root.urlRequestAction = "";
            if (!url) {
                root.urlResult = "Failed";
            } else if (action === "copy") {
                root.ui.copyText(url);
                root.urlResult = "Copied";
            } else {
                savedUrl.text = url;
            }
        }
    }
    onVisibleChanged: {
        if (!visible) {
            clearSavedUrl();
            importDialog.close();
        }
    }
    onCardColumnsChanged: ui.profileRows.columns = cardColumns
    Component.onCompleted: ui.profileRows.columns = cardColumns
    model: ui.profileRows
    reuseItems: true
    cacheBuffer: 0
    spacing: 12
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {
        id: scrollBar
        objectName: "subscription_page_bar"
        width: root.theme.scrollBarWidth
    }
    clip: true
    FileDialog {
        id: fileDialog
        title: qsTr("Import Clash Meta (mihomo) profile")
        nameFilters: [qsTr("YAML profiles (*.yaml *.yml)"), qsTr("All files (*)")]
        onAccepted: root.beginImport("import", [selectedFile.toString(), profileName.text])
    }
    ThemedDialog {
        id: importDialog
        objectName: "subscription_import_dialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        theme: root.theme
        modal: true
        title: qsTr("Add subscription")
        width: Math.min(520, Math.max(0, parent.width - 32))
        height: Math.min(importForm.implicitHeight + 2 * root.theme.focusMargin + header.implicitHeight + 2 * padding, parent.height - 32)
        initialFocusItem: subscriptionUrl
        property int profileCount: 0
        property bool attempted: false
        // Import is serialized by the runtime and this modal owns the only add entry.
        readonly property bool saved: attempted && root.ui.profileModel.count > profileCount
        onClosed: {
            subscriptionUrl.clear();
            profileName.clear();
            importRoute.currentIndex = 0;
            profileCount = 0;
            attempted = false;
        }
        contentItem: ThemedScrollView {
            id: importScroll
            objectName: "subscription_import_scroll"
            theme: root.theme
            form: importForm
            ColumnLayout {
                id: importForm
                spacing: 10
                ThemedField {
                    id: subscriptionUrl
                    theme: root.theme
                    objectName: "subscription_url"
                    placeholderText: qsTr("HTTPS subscription URL")
                    Accessible.name: qsTr("HTTPS subscription URL")
                    Layout.fillWidth: true
                    echoMode: TextInput.PasswordEchoOnEdit
                    maximumLength: 8192
                    enabled: !root.ui.runtime.busy
                }
                ThemedField {
                    id: profileName
                    theme: root.theme
                    objectName: "import_profile_name"
                    placeholderText: qsTr("Name (optional)")
                    Layout.fillWidth: true
                    maximumLength: 80
                    enabled: !root.ui.runtime.busy
                }
                ThemedComboBox {
                    id: importRoute
                    theme: root.theme
                    objectName: "subscription_import_route"
                    Layout.fillWidth: true
                    enabled: !root.ui.runtime.busy
                    model: root.downloadRouteLabels
                    Accessible.name: qsTr("Download path")
                }
                Label {
                    text: qsTr("Leave the name blank to use the subscription title or filename supplied by the server.")
                    color: root.theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
                Label {
                    objectName: "subscription_import_error"
                    visible: importDialog.attempted && !root.ui.runtime.busy && root.ui.runtimeError.length > 0
                    text: importDialog.saved ? qsTr("Subscription was added, but the operation reported an error: %1").arg(root.ui.runtimeError) : root.ui.runtimeError
                    color: root.theme.error
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    ActionButton {
                        width: Math.min(implicitWidth, parent.width)
                        objectName: "add_subscription"
                        theme: root.theme
                        text: root.ui.runtime.busy && importDialog.attempted ? qsTr("Working…") : qsTr("Add subscription")
                        pending: !!root.ui.runtime.busy && importDialog.attempted
                        enabled: !root.ui.runtime.busy && root.ui.settingsReady && !root.ui.runtime.readOnly && !importDialog.saved && subscriptionUrl.text.trim().length > 0
                        prominent: true
                        onClicked: root.beginImport("subscribe", [subscriptionUrl.text, profileName.text, root.downloadRoutes[importRoute.currentIndex]])
                    }
                    ActionButton {
                        width: Math.min(implicitWidth, parent.width)
                        objectName: "import_local_file"
                        theme: root.theme
                        text: qsTr("Import file")
                        enabled: !root.ui.runtime.busy && root.ui.settingsReady && !root.ui.runtime.readOnly && !importDialog.saved
                        onClicked: fileDialog.open()
                    }
                    ActionButton {
                        width: Math.min(implicitWidth, parent.width)
                        objectName: "cancel_subscription_import"
                        theme: root.theme
                        text: qsTr("Close")
                        onClicked: importDialog.close()
                    }
                }
            }
        }
    }
    Loader {
        id: editorLoader
        active: false
        sourceComponent: ProfileEditor {
            parent: Overlay.overlay
            ui: root.ui
            theme: root.theme
            onClosed: editorLoader.active = false
        }
    }
    ThemedDialog {
        id: rollbackConfirmation
        theme: root.theme
        parent: Overlay.overlay
        title: qsTr("Restore the selected profile version?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.ui.runtimeCommand("rollback_profile", [root.managedId, "confirmed", root.historyIndex.toString(), root.historyBase])
    }
    ThemedDialog {
        id: deleteConfirmation
        theme: root.theme
        parent: Overlay.overlay
        title: qsTr("Delete this subscription?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: Math.min(340, root.availableWidth - 60)
            text: qsTr("Deleting the selected subscription restores network settings and stops its session. Its custom rules and proxy binding are removed; global rules and other subscriptions are kept.")
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            management.close();
            root.ui.runtimeCommand("delete", [root.managedId]);
        }
    }
    ThemedDialog {
        id: management
        theme: root.theme
        parent: Overlay.overlay
        objectName: "subscription_management"
        title: qsTr("Subscription settings")
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, root.availableWidth - 16)
        height: Math.min(650, root.height - 24)
        standardButtons: Dialog.Close
        onAboutToHide: root.clearSavedUrl()
        onOpened: {
            root.clearSavedUrl();
            itemName.text = root.managedProfile.name || "";
            sourceUrl.clear();
            sourceHeaders.clear();
        }
        contentItem: ThemedScrollView {
            id: managementScroll
            theme: root.theme
            form: managementScrollForm
            ColumnLayout {
                id: managementScrollForm
                spacing: 12
                Label {
                    objectName: "subscription_managed_geo_notice"
                    visible: (root.managedProfile.compatibilityNotices || []).indexOf("ManagedGeo") >= 0
                    text: qsTr("This subscription specifies Geo download addresses. QuickClash uses its managed Geo data instead; rule matches may differ.")
                    color: root.theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
                Label {
                    objectName: "subscription_proxy_extension_notice"
                    visible: (root.managedProfile.compatibilityNotices || []).indexOf("IgnoredProxyExtensions") >= 0
                    text: qsTr("Third-party node metadata was retained in the original subscription but is not used by the core.")
                    color: root.theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
                ThemedField {
                    id: itemName
                    theme: root.theme
                    objectName: "subscription_name"
                    Layout.fillWidth: true
                    maximumLength: 80
                    placeholderText: qsTr("Name")
                }
                ActionButton {
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    theme: root.theme
                    text: qsTr("Rename")
                    enabled: !root.ui.runtime.busy && itemName.text.trim().length > 0
                    onClicked: root.ui.runtimeCommand("rename", [root.managedId, itemName.text])
                }
                Label {
                    visible: !!root.managedProfile.secretRef
                    text: qsTr("Saved subscription URL")
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.Wrap
                    color: root.theme.muted
                }
                ThemedField {
                    id: savedUrl
                    objectName: "subscription_saved_url"
                    theme: root.theme
                    Layout.fillWidth: true
                    visible: !!root.managedProfile.secretRef
                    readOnly: true
                    selectByMouse: true
                    placeholderText: qsTr("Hidden")
                    Accessible.name: qsTr("Saved subscription URL")
                }
                Flow {
                    visible: !!root.managedProfile.secretRef
                    Layout.fillWidth: true
                    spacing: 8
                    ActionButton {
                        width: Math.min(implicitWidth, parent.width)
                        objectName: "subscription_show_url"
                        theme: root.theme
                        text: savedUrl.text ? qsTr("Hide URL") : qsTr("Show URL")
                        enabled: !root.urlRequestToken
                        onClicked: {
                            if (savedUrl.text)
                                root.clearSavedUrl();
                            else
                                root.requestSavedUrl("show");
                        }
                    }
                    ActionButton {
                        width: Math.min(implicitWidth, parent.width)
                        objectName: "subscription_copy_url"
                        theme: root.theme
                        text: qsTr("Copy URL")
                        enabled: !root.urlRequestToken
                        onClicked: root.requestSavedUrl("copy")
                    }
                }
                Label {
                    visible: !!root.urlResult
                    text: root.urlResult === "Copied" ? qsTr("URL copied") : qsTr("Could not read the saved URL")
                    color: root.urlResult === "Copied" ? root.theme.muted : root.theme.error
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                ThemedComboBox {
                    id: updateInterval
                    objectName: "subscription_update_interval"
                    Accessible.name: qsTr("Update interval")
                    theme: root.theme
                    visible: !!root.managedProfile.secretRef
                    Layout.fillWidth: true
                    enabled: !root.ui.runtime.busy
                    property var minutes: [0, 60, 360, 720, 1440]
                    model: [qsTr("Manual updates"), qsTr("Every hour"), qsTr("Every 6 hours"), qsTr("Every 12 hours"), qsTr("Every day")]
                    currentIndex: Math.max(0, minutes.indexOf(root.managedProfile.updateIntervalMinutes || 0))
                    onActivated: root.ui.runtimeCommand("subscription_policy", [root.managedId, minutes[currentIndex].toString(), root.managedProfile.autoApply ? "true" : "false"])
                }
                ThemedSwitch {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    theme: root.theme
                    visible: !!root.managedProfile.secretRef
                    text: qsTr("Apply updates when selected")
                    checked: !!root.managedProfile.autoApply
                    enabled: !root.ui.runtime.busy
                    palette.windowText: root.theme.text
                    onClicked: root.ui.runtimeCommand("subscription_policy", [root.managedId, (root.managedProfile.updateIntervalMinutes || 0).toString(), checked ? "true" : "false"])
                }
                Label {
                    text: qsTr("Download path")
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.Wrap
                    color: root.theme.muted
                }
                ThemedComboBox {
                    theme: root.theme
                    objectName: "subscription_provider_mode"
                    visible: (root.managedProfile.managedProviders || []).length > 0
                    Layout.fillWidth: true
                    enabled: !root.ui.runtime.busy
                    model: [qsTr("Provider mode: import snapshot"), qsTr("Provider mode: core runtime")]
                    currentIndex: root.managedProfile.providerMode === "runtime" ? 1 : 0
                    onActivated: root.ui.runtimeCommand("provider_mode", [root.managedId, currentIndex === 1 ? "runtime" : "snapshot"])
                    Accessible.name: qsTr("Provider mode")
                }
                Label {
                    visible: (root.managedProfile.managedProviders || []).length > 0
                    text: qsTr("Switching provider mode downloads and validates all resources. Runtime refresh uses the core cache; history restores the configuration and fallback, not past remote content. Dynamic node selections are not restored automatically.")
                    color: root.theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                ThemedComboBox {
                    theme: root.theme
                    objectName: "subscription_download_route"
                    visible: !!root.managedProfile.secretRef
                    Layout.fillWidth: true
                    enabled: !root.ui.runtime.busy
                    model: root.downloadRouteLabels
                    currentIndex: Math.max(0, root.downloadRoutes.indexOf(root.managedProfile.downloadRoute || "Direct"))
                    onActivated: root.ui.runtimeCommand("subscription_route", [root.managedId, root.downloadRoutes[currentIndex]])
                    Accessible.name: qsTr("Download path")
                }
                Label {
                    visible: !!root.managedProfile.lastDownloadPath
                    text: root.managedProfile.lastDownloadPath === "RunningProxy" ? qsTr("Last download: running proxy") : qsTr("Last download: direct")
                    color: root.theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                ThemedField {
                    id: sourceUrl
                    theme: root.theme
                    objectName: "subscription_source_url"
                    Layout.fillWidth: true
                    placeholderText: qsTr("New HTTPS URL (leave blank to keep source)")
                    Accessible.name: qsTr("New HTTPS URL (leave blank to keep source)")
                    echoMode: TextInput.Password
                    maximumLength: 8192
                }
                ThemedField {
                    id: sourceHeaders
                    theme: root.theme
                    objectName: "subscription_source_headers"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Headers as JSON: Authorization, User-Agent, Accept")
                    echoMode: TextInput.Password
                    maximumLength: 8192
                }
                ActionButton {
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    theme: root.theme
                    text: qsTr("Validate and replace source")
                    enabled: !root.ui.runtime.busy && sourceUrl.text.length > 0
                    onClicked: {
                        root.ui.runtimeCommand("source", [root.managedId, sourceUrl.text, sourceHeaders.text]);
                        sourceUrl.clear();
                        sourceHeaders.clear();
                    }
                }
                ActionButton {
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    theme: root.theme
                    text: qsTr("Edit original YAML…")
                    enabled: !root.ui.runtime.busy
                    onClicked: root.editProfile(root.managedId, "source")
                }
                ActionButton {
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    theme: root.theme
                    text: qsTr("Edit profile overrides…")
                    enabled: !root.ui.runtime.busy
                    onClicked: root.editProfile(root.managedId, "overlay")
                }
                ThemedComboBox {
                    id: versions
                    theme: root.theme
                    Layout.fillWidth: true
                    visible: (root.managedProfile.history || []).length > 0
                    model: (root.managedProfile.history || []).map(function (version, index) {
                        return qsTr("Version %1 · %2").arg(index + 1).arg(version.updatedAt || "—");
                    })
                    Accessible.name: qsTr("Retained versions")
                }
                ActionButton {
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    theme: root.theme
                    text: qsTr("Restore selected version…")
                    visible: (root.managedProfile.history || []).length > 0
                    enabled: !root.ui.runtime.busy
                    onClicked: {
                        root.historyIndex = versions.currentIndex;
                        root.historyBase = root.managedProfile.revision || "";
                        rollbackConfirmation.open();
                    }
                }
                ActionButton {
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    theme: root.theme
                    text: qsTr("Delete")
                    destructive: true
                    enabled: !root.ui.runtime.busy
                    onClicked: deleteConfirmation.open()
                }
            }
        }
    }
    header: ColumnLayout {
        objectName: "subscription_content_header"
        width: root.availableWidth
        spacing: root.theme.gap
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: qsTr("Subscriptions")
                color: root.theme.text
                font.pixelSize: root.theme.bodySize
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
            }
            ActionButton {
                objectName: "open_subscription_import"
                theme: root.theme
                text: qsTr("Add subscription")
                prominent: true
                insetFocus: true
                enabled: !root.ui.runtime.busy && root.ui.settingsReady && !root.ui.runtime.readOnly
                onClicked: importDialog.open()
            }
        }
        Label {
            visible: !root.ui.profileModel.count
            text: qsTr("No subscriptions yet")
            color: root.theme.muted
            Layout.fillWidth: true
        }
        Item {
            implicitHeight: 2
            Layout.fillWidth: true
        }
    }
    delegate: Row {
        id: cardRow
        required property var first
        required property var second
        required property var third
        width: root.availableWidth
        height: Math.max(firstCard.implicitHeight, secondCard.height, thirdCard.height)
        function focusFirst() {
            firstCard.focusSelect();
        }
        spacing: 12
        readonly property real cardWidth: Math.max(0, (width - spacing * (root.cardColumns - 1)) / root.cardColumns)
        SubscriptionCard {
            id: firstCard
            width: cardRow.cardWidth
            height: implicitHeight
            ui: root.ui
            theme: root.theme
            profile: cardRow.first
            onManageRequested: profileId => root.manage(profileId)
            onActionRequested: (command, profileId) => root.ui.runtimeCommand(command, [profileId])
        }
        Loader {
            id: secondCard
            active: !!cardRow.second.id
            visible: active
            width: cardRow.cardWidth
            height: item ? item.implicitHeight : 0
            sourceComponent: SubscriptionCard {
                ui: root.ui
                theme: root.theme
                profile: cardRow.second
                onManageRequested: profileId => root.manage(profileId)
                onActionRequested: (command, profileId) => root.ui.runtimeCommand(command, [profileId])
            }
        }
        Loader {
            id: thirdCard
            active: !!cardRow.third.id
            visible: active
            width: cardRow.cardWidth
            height: item ? item.implicitHeight : 0
            sourceComponent: SubscriptionCard {
                ui: root.ui
                theme: root.theme
                profile: cardRow.third
                onManageRequested: profileId => root.manage(profileId)
                onActionRequested: (command, profileId) => root.ui.runtimeCommand(command, [profileId])
            }
        }
    }
}
