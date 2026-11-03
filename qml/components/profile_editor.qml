pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ThemedDialog {
    id: root
    required property UiState ui
    property string profileId: ""
    property string kind: "source"
    property string loadedToken: ""
    property bool loading: false
    property bool dirty: false
    property string pendingAction: ""
    readonly property string demandKey: ui.createDemandKey("editor")
    readonly property var state: ui.editor
    readonly property bool active: visible && ui.uiVisible
    readonly property bool ready: !!state.token && state.id === profileId && state.kind === kind
    readonly property bool previewReady: ready && !!state.previewDigest && !dirty
    objectName: "profile_editor"
    modal: true
    anchors.centerIn: parent
    width: Math.min(820, parent ? parent.width - 24 : 820)
    height: Math.min(780, parent ? parent.height - 24 : 780)
    title: kind === "source" ? qsTr("Edit original YAML") : qsTr("Edit profile overrides")
    standardButtons: Dialog.Close
    closePolicy: Popup.NoAutoClose
    function edit(id, layer) {
        profileId = id;
        kind = layer;
        loadedToken = "";
        dirty = false;
        open();
    }
    function saveBeforeRelease() {
        autosave.stop();
        if (loadedToken && dirty)
            ui.runtimeCommand("editor_close", [loadedToken, content.text]);
    }
    function perform(action) {
        autosave.stop();
        if (ready)
            ui.runtimeCommand(action, [state.token, content.text, copyName.text, "confirmed"]);
        if (action !== "editor_save_draft")
            dirty = false;
    }
    onActiveChanged: {
        if (!active) {
            saveBeforeRelease();
            confirmation.close();
            ui.setDemand(demandKey, "editor", false);
            ui.runtimeCommand("editor_close");
            loadedToken = "";
            content.clear();
            dirty = false;
        } else {
            ui.setDemand(demandKey, "editor", true);
            ui.runtimeCommand("editor_open", [profileId, kind]);
        }
    }
    Component.onDestruction: {
        saveBeforeRelease();
        ui.setDemand(demandKey, "editor", false);
    }
    Connections {
        target: root.ui
        function onUiAboutToHide() {
            root.saveBeforeRelease();
        }
        function onEditorChanged() {
            if (!root.ready || root.state.token === root.loadedToken)
                return;
            root.loading = true;
            root.loadedToken = root.state.token;
            content.text = root.state.text || "";
            copyName.text = (root.state.name || "") + qsTr(" (copy)");
            root.dirty = false;
            root.loading = false;
        }
    }
    Timer {
        id: autosave
        interval: 800
        onTriggered: root.perform("editor_save_draft")
    }
    ThemedDialog {
        id: confirmation
        parent: Overlay.overlay
        anchors.centerIn: parent
        theme: root.theme
        modal: true
        title: root.pendingAction === "editor_reload" ? qsTr("Discard draft and load latest?") : qsTr("Save and apply this configuration?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: Math.min(380, root.width - 48)
            wrapMode: Text.WordWrap
            color: root.theme.text
            text: root.pendingAction === "editor_reload" ? qsTr("This replaces the saved draft. Subscription updates and history are kept.") : qsTr("A running session may restart and interrupt connections. Validation or startup failures keep the draft and attempt to restore the last working session.")
        }
        onAccepted: {
            if (root.pendingAction === "editor_reload")
                root.ui.runtimeCommand("editor_open", [root.profileId, root.kind, "confirmed"]);
            else
                root.perform(root.pendingAction);
        }
    }
    contentItem: ThemedScrollView {
        id: scroll
        theme: root.theme
        form: scrollForm
        ColumnLayout {
            id: scrollForm
            spacing: 10
            Label {
                text: qsTr("Original content and overrides can contain credentials. Drafts are stored locally with restricted access. Editor limit: 1 MiB; larger profiles can be imported from a file.")
                color: root.theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("Draft base: %1 · Running revision: %2").arg(root.state.baseRevision || "—").arg(root.state.runningRevision || "—")
                color: root.theme.muted
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }
            Label {
                visible: !!root.state.conflict
                text: qsTr("The profile changed while this draft was open. Compare the previews, save a copy, or load the latest version.")
                color: root.theme.error
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: root.kind === "overlay"
                text: qsTr("JSON operations: set merges objects and replaces arrays; remove lists root fields; rules uses mode prepend, append or replace and an items array. Runtime listeners, TUN and controller fields are managed by settings.")
                color: root.theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ThemedTextArea {
                id: content
                objectName: "profile_editor_text"
                tabInsertsText: true
                theme: root.theme
                Layout.fillWidth: true
                Layout.preferredHeight: 260
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.NoWrap
                enabled: root.ready && !root.ui.runtime.busy
                Accessible.name: root.title
                onTextChanged: {
                    if (!root.loading && root.ready) {
                        root.dirty = true;
                        autosave.restart();
                    }
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                ActionButton {
                    width: Math.min(implicitWidth, parent.width)
                    theme: root.theme
                    text: qsTr("Save draft")
                    enabled: root.ready && !root.ui.runtime.busy
                    onClicked: root.perform("editor_save_draft")
                }
                ActionButton {
                    width: Math.min(implicitWidth, parent.width)
                    theme: root.theme
                    objectName: "profile_editor_preview"
                    text: qsTr("Validate and preview")
                    enabled: root.ready && !root.ui.runtime.busy
                    onClicked: root.perform("editor_preview")
                }
                ActionButton {
                    width: Math.min(implicitWidth, parent.width)
                    theme: root.theme
                    text: qsTr("Load latest…")
                    enabled: !root.ui.runtime.busy
                    onClicked: {
                        root.pendingAction = "editor_reload";
                        confirmation.open();
                    }
                }
            }
            Label {
                text: root.ui.runtime.busy ? qsTr("Validating or applying…") : root.state.candidateSaved ? qsTr("Candidate saved. Check the subscription's pending state and the current running status.") : root.state.draftSaved ? qsTr("Draft saved locally") : ""
                color: root.theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: root.previewReady
                text: qsTr("Source → profile overrides (%1) → global mode, DNS, ports and TUN → reserved controller and resource constraints. Previews hide credentials and may omit content above 256 KiB.").arg((root.state.overlayFields || []).join(", "))
                color: root.theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: root.previewReady
                text: qsTr("Current profile (redacted)")
                color: root.theme.text
            }
            ThemedTextArea {
                theme: root.theme
                visible: root.previewReady
                text: root.state.previousPreview || ""
                Accessible.name: qsTr("Current profile (redacted)")
                textFormat: TextEdit.PlainText
                readOnly: true
                Layout.fillWidth: true
                Layout.preferredHeight: 140
            }
            Label {
                visible: root.previewReady
                text: qsTr("Draft effective configuration (redacted)")
                color: root.theme.text
            }
            ThemedTextArea {
                theme: root.theme
                visible: root.previewReady
                text: root.state.preview || ""
                Accessible.name: qsTr("Draft effective configuration (redacted)")
                textFormat: TextEdit.PlainText
                readOnly: true
                Layout.fillWidth: true
                Layout.preferredHeight: 180
            }
            ThemedField {
                id: copyName
                theme: root.theme
                visible: root.previewReady
                maximumLength: 80
                placeholderText: qsTr("Copy name")
                Layout.fillWidth: true
            }
            Flow {
                visible: root.previewReady
                Layout.fillWidth: true
                spacing: 8
                ActionButton {
                    width: Math.min(implicitWidth, parent.width)
                    theme: root.theme
                    text: qsTr("Save and apply…")
                    prominent: true
                    enabled: !root.ui.runtime.busy && !root.state.conflict
                    onClicked: {
                        root.pendingAction = "editor_apply";
                        confirmation.open();
                    }
                }
                ActionButton {
                    width: Math.min(implicitWidth, parent.width)
                    theme: root.theme
                    text: qsTr("Save as a local copy…")
                    enabled: !root.ui.runtime.busy && copyName.text.trim().length > 0
                    onClicked: {
                        root.pendingAction = "editor_copy";
                        confirmation.open();
                    }
                }
            }
        }
    }
}
