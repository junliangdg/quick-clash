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
    property bool editingSource: false
    property bool loadingSource: false
    property bool sourceDirty: false
    property bool sourceSaving: false
    property string sourceBaseline: ""
    property string sourceOperationId: ""
    property string sourceOperationToken: ""
    property string sourceError: ""
    property string sourceCloseAction: ""
    property bool suppressSourceCloseRejection: false
    property bool sourceRevealed: false
    property bool sourceHasHeaders: false
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
        sourceRevealed = false;
        if (!editingSource) {
            savedUrl.clear();
            sourceBaseline = "";
        }
    }
    function resetSourceEditor() {
        editingSource = false;
        loadingSource = false;
        sourceDirty = false;
        sourceBaseline = "";
        sourceError = "";
        sourceHeaders.clear();
        sourceHasHeaders = false;
        clearSavedUrl();
        ui.setSubscriptionAddressDirty(false);
    }
    function editSource() {
        sourceError = "";
        if (!managedProfile.secretRef) {
            beginSourceEdit("");
        } else if (savedUrl.text.length > 0) {
            beginSourceEdit(savedUrl.text);
        } else {
            loadingSource = true;
            requestSavedUrl("edit");
        }
    }
    function beginSourceEdit(url) {
        sourceBaseline = url;
        savedUrl.text = url;
        editingSource = true;
        loadingSource = false;
        sourceRevealed = true;
        savedUrl.forceActiveFocus(Qt.TabFocusReason);
        savedUrl.selectAll();
    }
    function submitSource() {
        if (!editingSource || sourceSaving || ui.runtime.busy || !savedUrl.text.trim())
            return;
        const closeAction = sourceCloseAction;
        sourceCloseAction = "";
        suppressSourceCloseRejection = true;
        sourceCloseConfirmation.close();
        suppressSourceCloseRejection = false;
        sourceOperationId = managedId;
        sourceOperationToken = Date.now().toString() + ":" + Math.random().toString();
        sourceSaving = true;
        sourceCloseAction = closeAction;
        sourceError = "";
        ui.setSubscriptionAddressDirty(false);
        ui.setSubscriptionAddressSaving(true);
        ui.runtimeCommand("source", [sourceOperationId, savedUrl.text, sourceHeaders.text, sourceOperationToken]);
    }
    function checkSourceDirty() {
        sourceDirty = savedUrl.text !== sourceBaseline || sourceHasHeaders;
        ui.setSubscriptionAddressDirty(sourceDirty && !sourceSaving);
    }
    function requestSourceClose(action) {
        if (action !== "navigate")
            ui.clearSubscriptionAddressNavigation();
        if (sourceSaving) {
            sourceCloseAction = action;
            return;
        }
        if (action === "cancel_edit" && !sourceDirty) {
            resetSourceEditor();
            return;
        }
        sourceCloseAction = action;
        if (editingSource && sourceDirty && !sourceSaving) {
            sourceCloseConfirmation.open();
            return;
        }
        finishSourceClose();
    }
    function finishSourceClose() {
        const action = sourceCloseAction;
        sourceCloseAction = "";
        suppressSourceCloseRejection = true;
        sourceCloseConfirmation.close();
        suppressSourceCloseRejection = false;
        resetSourceEditor();
        if (action === "dialog")
            management.close();
        else if (action === "navigate") {
            management.close();
            Qt.callLater(function () { root.ui.resumeSubscriptionAddressNavigation(); });
        }
        else if (action === "cancel_edit")
            return;
        else if (action === "close")
            ui.requestClose();
        else if (action === "quit")
            ui.requestQuit();
    }
    function requestSavedUrl(action) {
        urlRequestToken = "";
        urlRequestAction = "";
        urlResult = "";
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
    function linkAccessibleLabel(field, label) {
        // Dynamic lookup keeps Qt 6.9 compatible; the property appeared in Qt 6.10.
        const linker = Qt.createQmlObject('import QtQuick; Item { function link(field, label) { if (field.Accessible.labelledBy !== undefined) field.Accessible.labelledBy = label; } }', root, "accessibleLabelLink");
        try {
            // The checker cannot see methods defined in the dynamic component.
            // qmllint disable missing-property
            linker.link(field, label);
            // qmllint enable missing-property
        } finally {
            linker.destroy();
        }
    }
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
        if ((managedProfile.secretRef || "") !== previousSource && !editingSource)
            clearSavedUrl();
        if (!managedProfile.id) {
            rollbackConfirmation.close();
            deleteConfirmation.close();
            management.close();
            resetSourceEditor();
            managedId = "";
        } else if (itemName.text === previousName || !itemName.activeFocus) {
            itemName.text = managedProfile.name || "";
        }
    }
    function manage(profileId) {
        resetSourceEditor();
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
                if (root.editingSource)
                    root.sourceRevealed = false;
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
                root.loadingSource = false;
            } else if (action === "copy") {
                root.ui.copyText(url);
                root.urlResult = "Copied";
            } else if (action === "edit") {
                root.beginSourceEdit(url);
            } else {
                savedUrl.text = url;
                savedUrl.cursorPosition = 0;
                root.sourceRevealed = true;
            }
        }
        function onSubscriptionSourceFinished(profileId: string, token: string, saved: bool, errorCode: string) {
            if (profileId !== root.sourceOperationId || token !== root.sourceOperationToken || !token)
                return;
            root.sourceSaving = false;
            root.ui.setSubscriptionAddressSaving(false);
            root.sourceOperationId = "";
            root.sourceOperationToken = "";
            if (saved && !errorCode && root.sourceCloseAction) {
                root.finishSourceClose();
                return;
            }
            root.sourceCloseAction = "";
            root.ui.clearSubscriptionAddressNavigation();
            if (!management.visible || profileId !== root.managedId) {
                root.ui.setSubscriptionAddressDirty(false);
                return;
            }
            if (saved) {
                root.resetSourceEditor();
                root.urlResult = errorCode ? "SavedApplyFailed" : "Saved";
                root.sourceError = errorCode ? root.ui.errorText(errorCode) : "";
            } else {
                root.sourceError = root.ui.errorText(errorCode);
                root.sourceDirty = true;
                root.ui.setSubscriptionAddressDirty(true);
            }
        }
        function onSubscriptionAddressCloseRequested(action: string) {
            root.requestSourceClose(action);
        }
    }
    onVisibleChanged: {
        if (!visible) {
            if (editingSource)
                sourceRevealed = false;
            clearSavedUrl();
            importDialog.close();
        }
    }
    Component.onDestruction: ui.setSubscriptionAddressDirty(false)
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
        title: qsTr("Import Mihomo profile")
        nameFilters: [qsTr("Configurations and node lists (*.yaml *.yml *.json *.txt *.conf)"), qsTr("All files (*)")]
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
                Label {
                    id: importUrlLabel
                    text: qsTr("Subscription URL (HTTP/HTTPS)")
                    color: root.theme.text
                    Layout.fillWidth: true
                }
                ThemedField {
                    id: subscriptionUrl
                    theme: root.theme
                    objectName: "subscription_url"
                    placeholderText: qsTr("Subscription URL (HTTP/HTTPS)")
                    Accessible.name: qsTr("Subscription URL (HTTP/HTTPS)")
                    // Qt suppresses the accessible name for password edits. Keep
                    // a non-secret description on all supported Qt versions.
                    Accessible.description: importUrlLabel.text
                    Component.onCompleted: root.linkAccessibleLabel(subscriptionUrl, importUrlLabel)
                    Layout.fillWidth: true
                    echoMode: TextInput.PasswordEchoOnEdit
                    maximumLength: 8192
                    enabled: !root.ui.runtime.busy
                }
                Label {
                    text: qsTr("Name (optional)")
                    color: root.theme.text
                    Layout.fillWidth: true
                }
                ThemedField {
                    id: profileName
                    theme: root.theme
                    objectName: "import_profile_name"
                    placeholderText: qsTr("Name (optional)")
                    Accessible.name: qsTr("Name (optional)")
                    Layout.fillWidth: true
                    maximumLength: 80
                    enabled: !root.ui.runtime.busy
                }
                Label {
                    text: qsTr("Download path")
                    color: root.theme.text
                    Layout.fillWidth: true
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
        id: sourceCloseConfirmation
        objectName: "subscription_source_close_confirmation"
        theme: root.theme
        parent: Overlay.overlay
        title: qsTr("The subscription URL has not been saved")
        modal: true
        anchors.centerIn: parent
        closePolicy: Popup.CloseOnEscape
        onRejected: {
            if (!root.suppressSourceCloseRejection) {
                root.sourceCloseAction = "";
                root.ui.clearSubscriptionAddressNavigation();
            }
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: qsTr("Save and update the subscription, discard the changes, or continue editing.")
                color: root.theme.text
                wrapMode: Text.WordWrap
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                ActionButton {
                    objectName: "subscription_close_save"
                    theme: root.theme
                    prominent: true
                    text: qsTr("Save and update")
                    enabled: !root.ui.runtime.busy && !root.sourceSaving && savedUrl.text.trim().length > 0
                    onClicked: root.submitSource()
                }
                ActionButton {
                    objectName: "subscription_close_discard"
                    theme: root.theme
                    text: qsTr("Discard changes")
                    onClicked: root.finishSourceClose()
                }
                ActionButton {
                    objectName: "subscription_close_continue"
                    theme: root.theme
                    text: qsTr("Continue editing")
                    onClicked: { root.sourceCloseAction = ""; root.ui.clearSubscriptionAddressNavigation(); sourceCloseConfirmation.close(); }
                }
            }
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
        objectName: "subscription_delete_confirmation"
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
            root.resetSourceEditor();
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
        width: Math.min(640, Math.max(0, root.availableWidth - 16))
        height: Math.min(650, root.height - 24)
        closePolicy: Popup.NoAutoClose
        property bool authorizedHide: false
        footer: DialogButtonBox {
            padding: root.theme.panelPadding
            ActionButton {
                objectName: "subscription_management_close"
                theme: root.theme
                text: qsTr("Close")
                onClicked: root.requestSourceClose("dialog")
            }
            background: Item {}
        }
        onOpened: {
            authorizedHide = false;
            if (!root.editingSource || !root.sourceDirty)
                root.resetSourceEditor();
            itemName.text = root.managedProfile.name || "";
        }
        onClosed: {
            if (root.editingSource && root.sourceDirty && !authorizedHide) {
                management.open();
                root.requestSourceClose("dialog");
            } else {
                root.resetSourceEditor();
            }
            authorizedHide = false;
        }
        contentItem: ThemedScrollView {
            id: managementScroll
            objectName: "subscription_management_scroll"
            Keys.onEscapePressed: event => { root.requestSourceClose("dialog"); event.accepted = true; }
            theme: root.theme
            form: managementScrollForm
            ColumnLayout {
                id: managementScrollForm
                objectName: "subscription_management_form"
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
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
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
                    id: savedUrlLabel
                    text: qsTr("Subscription URL")
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.Wrap
                    color: root.theme.text
                    font.weight: Font.DemiBold
                }
                ThemedField {
                    id: savedUrl
                    objectName: "subscription_saved_url"
                    theme: root.theme
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    readOnly: !root.editingSource || root.sourceSaving
                    selectByMouse: true
                    placeholderText: root.managedProfile.secretRef ? qsTr("Hidden") : qsTr("No subscription URL")
                    Accessible.name: qsTr("Subscription URL")
                    Accessible.description: savedUrlLabel.text
                    Component.onCompleted: root.linkAccessibleLabel(savedUrl, savedUrlLabel)
                    echoMode: root.sourceRevealed ? TextInput.Normal : TextInput.Password
                    maximumLength: 8192
                    onTextChanged: {
                        if (root.editingSource)
                            root.checkSourceDirty();
                        root.sourceError = "";
                    }
                    onAccepted: if (root.editingSource) root.submitSource()
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 8
                    ActionButton {
                        width: Math.min(implicitWidth, parent.width)
                        objectName: "subscription_show_url"
                        theme: root.theme
                        visible: !!root.managedProfile.secretRef || root.editingSource
                        text: root.sourceRevealed ? qsTr("Hide URL") : qsTr("Show URL")
                        enabled: !root.urlRequestToken && !root.sourceSaving
                        onClicked: {
                            if (root.editingSource)
                                root.sourceRevealed = !root.sourceRevealed;
                            else if (savedUrl.text)
                                root.clearSavedUrl();
                            else
                                root.requestSavedUrl("show");
                        }
                    }
                    ActionButton {
                        width: Math.min(implicitWidth, parent.width)
                        objectName: "subscription_copy_url"
                        theme: root.theme
                        visible: !!root.managedProfile.secretRef || root.editingSource
                        text: qsTr("Copy URL")
                        enabled: !root.urlRequestToken && !root.sourceSaving
                        onClicked: {
                            if (root.editingSource) {
                                root.ui.copyText(savedUrl.text);
                                root.urlResult = "Copied";
                            } else
                                root.requestSavedUrl("copy");
                        }
                    }
                    ActionButton {
                        width: Math.min(implicitWidth, parent.width)
                        objectName: "subscription_edit_url"
                        theme: root.theme
                        text: root.managedProfile.secretRef ? qsTr("Edit") : qsTr("Add subscription URL")
                        visible: !root.editingSource
                        enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly && !root.urlRequestToken && !root.sourceSaving
                        onClicked: root.editSource()
                    }
                }
                Label {
                    visible: root.editingSource
                    text: qsTr("Saving downloads and checks the new URL. A running subscription may restart and interrupt connections.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.WordWrap
                }
                ActionButton {
                    id: headersToggle
                    objectName: "subscription_headers_toggle"
                    theme: root.theme
                    visible: root.editingSource
                    property bool expanded: false
                    text: expanded ? qsTr("Hide request headers") : qsTr("Request headers (optional)")
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    onClicked: expanded = !expanded
                }
                Label {
                    visible: root.editingSource
                    text: qsTr("Existing custom headers are not carried over. Enter any headers required by the new URL.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.WordWrap
                }
                ThemedField {
                    id: sourceHeaders
                    theme: root.theme
                    objectName: "subscription_source_headers"
                    visible: headersToggle.expanded && root.editingSource
                    enabled: !root.sourceSaving
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    placeholderText: qsTr("Headers as JSON: Authorization, User-Agent, Accept")
                    Accessible.name: qsTr("Request headers (optional)")
                    Accessible.description: qsTr("Request headers (optional)")
                    echoMode: TextInput.Password
                    maximumLength: 8192
                    onTextChanged: {
                        root.sourceHasHeaders = text.length > 0;
                        if (root.editingSource)
                            root.checkSourceDirty();
                    }
                }
                Flow {
                    visible: root.editingSource
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 8
                    ActionButton {
                        width: Math.min(implicitWidth, parent.width)
                        objectName: "subscription_validate_source"
                        theme: root.theme
                        prominent: true
                        text: root.sourceSaving ? qsTr("Downloading and checking…") : qsTr("Save and update")
                        pending: root.sourceSaving
                        enabled: !root.ui.runtime.busy && !root.sourceSaving && savedUrl.text.trim().length > 0
                        onClicked: root.submitSource()
                    }
                    ActionButton {
                        width: Math.min(implicitWidth, parent.width)
                        objectName: "subscription_cancel_source"
                        theme: root.theme
                        text: qsTr("Cancel")
                        enabled: !root.sourceSaving
                        onClicked: root.requestSourceClose("cancel_edit")
                    }
                }
                Label {
                    objectName: "subscription_source_result"
                    visible: !!root.urlResult || !!root.sourceError
                    text: root.sourceError ? (root.urlResult === "SavedApplyFailed" ? qsTr("URL saved; applying failed: %1").arg(root.sourceError) : root.sourceError) : root.urlResult === "Copied" ? qsTr("URL copied") : root.urlResult === "Saved" ? qsTr("URL saved and subscription updated") : qsTr("Could not read the saved URL")
                    color: root.sourceError || root.urlResult === "Failed" ? root.theme.error : root.theme.muted
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.WordWrap
                }
                ThemedComboBox {
                    id: updateInterval
                    objectName: "subscription_update_interval"
                    Accessible.name: qsTr("Update interval")
                    theme: root.theme
                    visible: !!root.managedProfile.secretRef
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    enabled: !root.ui.runtime.busy
                    property var minutes: [0, 60, 360, 720, 1440]
                    model: [qsTr("Manual updates"), qsTr("Every hour"), qsTr("Every 6 hours"), qsTr("Every 12 hours"), qsTr("Every day")]
                    currentIndex: Math.max(0, minutes.indexOf(root.managedProfile.updateIntervalMinutes || 0))
                    onActivated: root.ui.runtimeCommand("subscription_policy", [root.managedId, minutes[currentIndex].toString(), root.managedProfile.autoApply ? "true" : "false"])
                }
                ThemedSwitch {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    theme: root.theme
                    visible: !!root.managedProfile.secretRef
                    text: qsTr("Apply automatic updates to the current subscription")
                    checked: !!root.managedProfile.autoApply
                    enabled: !root.ui.runtime.busy
                    palette.windowText: root.theme.text
                    onClicked: root.ui.runtimeCommand("subscription_policy", [root.managedId, (root.managedProfile.updateIntervalMinutes || 0).toString(), checked ? "true" : "false"])
                }
                ThemedComboBox {
                    theme: root.theme
                    objectName: "subscription_provider_mode"
                    visible: (root.managedProfile.managedProviders || []).length > 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
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
                    Layout.minimumWidth: 0
                }
                Label {
                    visible: !!root.managedProfile.secretRef
                    text: qsTr("Download path")
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.Wrap
                    color: root.theme.text
                    font.weight: Font.DemiBold
                }
                ThemedComboBox {
                    theme: root.theme
                    objectName: "subscription_download_route"
                    visible: !!root.managedProfile.secretRef
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
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
                    Layout.minimumWidth: 0
                }
                ActionButton {
                    id: advancedButton
                    objectName: "subscription_advanced"
                    theme: root.theme
                    property bool expanded: false
                    text: expanded ? qsTr("Hide advanced settings") : qsTr("Advanced settings")
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    onClicked: expanded = !expanded
                }
                Flow {
                    objectName: "subscription_editor_actions"
                    visible: advancedButton.expanded
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 8
                    ActionButton {
                        width: Math.min(implicitWidth, parent.width)
                        objectName: "subscription_edit_source"
                        theme: root.theme
                        text: qsTr("Edit original YAML…")
                        enabled: !root.ui.runtime.busy
                        onClicked: root.editProfile(root.managedId, "source")
                    }
                    ActionButton {
                        width: Math.min(implicitWidth, parent.width)
                        objectName: "subscription_edit_overrides"
                        theme: root.theme
                        text: qsTr("Edit profile overrides…")
                        enabled: !root.ui.runtime.busy
                        onClicked: root.editProfile(root.managedId, "overlay")
                    }
                }
                ThemedComboBox {
                    id: versions
                    objectName: "subscription_versions"
                    theme: root.theme
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    visible: advancedButton.expanded && (root.managedProfile.history || []).length > 0
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
                    visible: advancedButton.expanded && (root.managedProfile.history || []).length > 0
                    enabled: !root.ui.runtime.busy
                    onClicked: {
                        root.historyIndex = versions.currentIndex;
                        root.historyBase = root.managedProfile.revision || "";
                        rollbackConfirmation.open();
                    }
                }
                ActionButton {
                    objectName: "subscription_delete"
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: managementScroll.formWidth
                    theme: root.theme
                    text: qsTr("Delete")
                    destructive: true
                    enabled: !root.ui.runtime.busy && !root.sourceSaving
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
            text: qsTr("No subscriptions yet. Add a subscription URL or import a local file.")
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
