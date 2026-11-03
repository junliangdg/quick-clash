pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ThemedDialog {
    id: root
    required property UiState ui
    property var definition: ({})
    property var selectedProviders: []
    property bool loading: false
    property bool dirty: true
    property bool removing: false
    property bool saving: false
    property string baseRevision: ""
    readonly property var state: ui.customGroups
    readonly property var preview: state.preview || ({})
    readonly property bool conflict: visible && baseRevision !== (state.revision || "")
    readonly property bool previewReady: !dirty && !conflict && !!preview.validated && !!preview.token
    readonly property var previewGroup: (preview.groups || []).find(entry => entry.id === preview.target) || ({})
    objectName: "custom_group_editor"
    title: removing ? qsTr("Delete custom group") : qsTr("Custom strategy group")
    modal: true
    anchors.centerIn: parent
    width: Math.min(700, parent ? parent.width - 24 : 700)
    height: Math.min(780, parent ? parent.height - 24 : 780)
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape
    initialFocusItem: nameField
    function invalidate(): void {
        if (!loading)
            dirty = true;
    }
    function preset(index): void {
        if (index === 0)
            return;
        nameField.text = ["", qsTr("All nodes automatic"), qsTr("Hong Kong automatic"), qsTr("Japan automatic"), qsTr("Singapore automatic"), qsTr("Failover")][index];
        includeField.text = ["", "", "香港,HK,Hong Kong", "日本,JP,Japan", "新加坡,SG,Singapore", ""][index];
        filterMode.currentIndex = 0;
        strategy.currentIndex = index === 5 ? 1 : 0;
        invalidate();
    }
    function edit(spec, remove = false): void {
        loading = true;
        definition = JSON.parse(JSON.stringify(spec || {}));
        removing = remove;
        saving = false;
        baseRevision = state.revision || "";
        selectedProviders = (definition.providers || []).slice();
        nameField.text = definition.name || "";
        strategy.currentIndex = Math.max(0, ["url-test", "fallback", "load-balance"].indexOf(definition.type || "url-test"));
        inlineSource.checked = definition.inline === undefined ? true : definition.inline;
        filterMode.currentIndex = definition.mode === "regex" ? 1 : 0;
        includeField.text = definition.include || "";
        excludeField.text = definition.exclude || "";
        target.text = definition.url || "https://www.gstatic.com/generate_204";
        interval.value = definition.interval || 300;
        timeout.value = definition.timeout || 2000;
        tolerance.value = definition.tolerance === undefined ? 50 : definition.tolerance;
        lazy.checked = !!definition.lazy;
        balancing.currentIndex = Math.max(0, ["consistent-hashing", "round-robin", "sticky-sessions"].indexOf(definition.strategy || "consistent-hashing"));
        parentGroup.currentIndex = definition.parent ? (state.parents || []).indexOf(definition.parent) + 1 : 0;
        activate.checked = false;
        templatePicker.currentIndex = 0;
        dirty = true;
        loading = false;
        open();
        ui.runtimeCommand("custom_group_clear");
    }
    function spec(): var {
        const result = {
            id: definition.id || "",
            name: nameField.text.trim(),
            type: ["url-test", "fallback", "load-balance"][strategy.currentIndex],
            inline: inlineSource.checked,
            providers: selectedProviders,
            mode: filterMode.currentIndex === 1 ? "regex" : "keywords",
            include: includeField.text,
            exclude: excludeField.text,
            url: target.text.trim(),
            interval: interval.value,
            timeout: timeout.value,
            tolerance: tolerance.value,
            lazy: lazy.checked,
            strategy: ["consistent-hashing", "round-robin", "sticky-sessions"][balancing.currentIndex],
            parent: parentGroup.currentIndex > 0 ? (state.parents || [])[parentGroup.currentIndex - 1] : ""
        };
        if (definition.sourceNames !== undefined)
            result.sourceNames = definition.sourceNames;
        return result;
    }
    onClosed: {
        confirmation.close();
        ui.runtimeCommand("custom_group_clear");
        definition = ({});
        selectedProviders = [];
        nameField.clear();
        includeField.clear();
        excludeField.clear();
    }
    Connections {
        target: root.ui
        function onCustomGroupsChanged(): void {
            if (!root.saving)
                return;
            if (root.state.result === "Saved") {
                root.saving = false;
                root.close();
            } else if (root.state.result === "Failed") {
                root.saving = false;
                root.dirty = true;
            }
        }
        function onUiAboutToHide(): void {
            root.close();
        }
    }
    ThemedDialog {
        id: confirmation
        objectName: "custom_group_confirmation"
        parent: Overlay.overlay
        theme: root.theme
        anchors.centerIn: parent
        modal: true
        title: root.removing ? qsTr("Delete this group?") : qsTr("Apply this strategy group?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: Math.min(420, root.width - 64)
            text: qsTr("Applying may restart the running session and interrupt connections. A failure keeps the last working session. Creating a group alone does not change routing.")
            wrapMode: Text.WordWrap
            color: root.theme.text
        }
        onAccepted: {
            if (root.previewReady) {
                root.saving = true;
                root.ui.runtimeCommand("custom_group_apply", [root.preview.token, "confirmed"]);
            }
        }
    }
    contentItem: ThemedScrollView {
        id: scroll
        theme: root.theme
        form: fields
        objectName: "custom_group_scroll"
        ColumnLayout {
            id: fields
            spacing: 10
            enabled: !root.ui.runtime.busy && !root.saving
            Label {
                visible: root.conflict
                text: qsTr("The profile changed. Close and reopen this form before previewing again.")
                color: root.theme.warning
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Label {
                visible: !!root.ui.runtimeError
                text: root.ui.runtimeError
                color: root.theme.error
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            ThemedComboBox {
                id: templatePicker
                objectName: "custom_group_template"
                theme: root.theme
                Layout.fillWidth: true
                visible: !root.definition.id && !root.removing
                model: [qsTr("Choose a template (optional)"), qsTr("All nodes automatic"), qsTr("Hong Kong automatic"), qsTr("Japan automatic"), qsTr("Singapore automatic"), qsTr("Failover")]
                Accessible.name: qsTr("Group template")
                onActivated: root.preset(currentIndex)
            }
            ThemedField {
                id: nameField
                objectName: "custom_group_name"
                theme: root.theme
                Layout.fillWidth: true
                placeholderText: qsTr("Group name")
                Accessible.name: qsTr("Group name")
                maximumLength: 80
                readOnly: root.removing
                onTextEdited: root.invalidate()
            }
            ColumnLayout {
                Layout.fillWidth: true
                enabled: !root.removing
                spacing: 10
                ThemedComboBox {
                    id: strategy
                    objectName: "custom_group_strategy"
                    theme: root.theme
                    Layout.fillWidth: true
                    model: [qsTr("Automatic latency selection"), qsTr("Ordered failover"), qsTr("Load balancing")]
                    Accessible.name: qsTr("Selection strategy")
                    onActivated: root.invalidate()
                }
                Label {
                    text: qsTr("Node sources")
                    color: root.theme.text
                    font.bold: true
                }
                ThemedCheckBox {
                    id: inlineSource
                    theme: root.theme
                    text: qsTr("Inline nodes (including imported snapshots)")
                    Layout.fillWidth: true
                    onToggled: root.invalidate()
                }
                Repeater {
                    model: root.state.providers || []
                    ThemedCheckBox {
                        required property string modelData
                        theme: root.theme
                        text: qsTr("Provider: %1").arg(modelData)
                        Layout.fillWidth: true
                        checked: root.selectedProviders.includes(modelData)
                        onToggled: {
                            root.selectedProviders = checked ? root.selectedProviders.concat([modelData]) : root.selectedProviders.filter(name => name !== modelData);
                            root.invalidate();
                        }
                    }
                }
                Label {
                    text: root.definition.sourceNames !== undefined ? qsTr("This restored group's source names are frozen to its offline archive.") : qsTr("Regions match node names, not IP locations. Exclusions win. Empty include matches all source nodes.")
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: root.theme.muted
                }
                ThemedComboBox {
                    id: filterMode
                    objectName: "custom_group_filter_mode"
                    theme: root.theme
                    Layout.fillWidth: true
                    model: [qsTr("Keywords (comma separated, any match)"), qsTr("Restricted regular expression")]
                    Accessible.name: qsTr("Name matching mode")
                    onActivated: root.invalidate()
                }
                ThemedField {
                    id: includeField
                    objectName: "custom_group_include"
                    theme: root.theme
                    Layout.fillWidth: true
                    placeholderText: qsTr("Include names (optional)")
                    Accessible.name: qsTr("Include names")
                    maximumLength: 512
                    onTextEdited: root.invalidate()
                }
                ThemedField {
                    id: excludeField
                    objectName: "custom_group_exclude"
                    theme: root.theme
                    Layout.fillWidth: true
                    placeholderText: qsTr("Exclude names (optional)")
                    Accessible.name: qsTr("Exclude names")
                    maximumLength: 512
                    onTextEdited: root.invalidate()
                }
                ThemedComboBox {
                    id: parentGroup
                    objectName: "custom_group_parent"
                    theme: root.theme
                    Layout.fillWidth: true
                    model: [qsTr("No parent group")].concat(root.state.parents || [])
                    Accessible.name: qsTr("Parent selector")
                    onActivated: root.invalidate()
                }
                ThemedCheckBox {
                    id: activate
                    theme: root.theme
                    enabled: parentGroup.currentIndex > 0
                    text: qsTr("Select this group in the parent after applying")
                    Layout.fillWidth: true
                    onToggled: root.invalidate()
                }
                Label {
                    Layout.fillWidth: true
                    visible: root.selectedProviders.length > 0
                    text: qsTr("Provider previews use the last core observation, or the imported snapshot when stopped. Membership may change after preview.")
                    color: root.theme.muted
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: qsTr("Probe settings")
                    color: root.theme.text
                    font.bold: true
                }
                ThemedField {
                    id: target
                    objectName: "custom_group_url"
                    theme: root.theme
                    Layout.fillWidth: true
                    placeholderText: qsTr("HTTP(S) probe URL")
                    Accessible.name: qsTr("HTTP(S) probe URL")
                    maximumLength: 4096
                    onTextEdited: root.invalidate()
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        Label {
                            text: qsTr("Interval (seconds)")
                            color: root.theme.text
                        }
                        ThemedSpinBox {
                            id: interval
                            objectName: "custom_group_interval"
                            theme: root.theme
                            from: 1
                            to: 86400
                            editable: true
                            Accessible.name: qsTr("Interval (seconds)")
                            onValueModified: root.invalidate()
                        }
                    }
                    ColumnLayout {
                        Label {
                            text: qsTr("Timeout (milliseconds)")
                            color: root.theme.text
                        }
                        ThemedSpinBox {
                            id: timeout
                            objectName: "custom_group_timeout"
                            theme: root.theme
                            from: 1
                            to: 60000
                            editable: true
                            Accessible.name: qsTr("Timeout (milliseconds)")
                            onValueModified: root.invalidate()
                        }
                    }
                    ColumnLayout {
                        visible: strategy.currentIndex === 0
                        Label {
                            text: qsTr("Tolerance (milliseconds)")
                            color: root.theme.text
                        }
                        ThemedSpinBox {
                            id: tolerance
                            objectName: "custom_group_tolerance"
                            theme: root.theme
                            from: 0
                            to: 60000
                            editable: true
                            Accessible.name: qsTr("Tolerance (milliseconds)")
                            onValueModified: root.invalidate()
                        }
                    }
                }
                ThemedCheckBox {
                    id: lazy
                    theme: root.theme
                    text: qsTr("Lazy probes (only when the group is used)")
                    Layout.fillWidth: true
                    onToggled: root.invalidate()
                }
                ThemedComboBox {
                    id: balancing
                    theme: root.theme
                    Layout.fillWidth: true
                    visible: strategy.currentIndex === 2
                    model: [qsTr("Consistent hashing"), qsTr("Round robin"), qsTr("Sticky sessions")]
                    Accessible.name: qsTr("Load balancing strategy")
                    onActivated: root.invalidate()
                }
                Label {
                    text: qsTr("Selected providers share these health-check settings with their other groups. Conflicting settings across custom groups are rejected. Other groups may still probe excluded nodes.")
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: root.theme.muted
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                ActionButton {
                    width: Math.min(implicitWidth, parent.width)
                    objectName: "custom_group_preview"
                    theme: root.theme
                    text: root.removing ? qsTr("Preview removal") : qsTr("Preview candidates")
                    enabled: !root.conflict && !!root.baseRevision
                    onClicked: {
                        root.dirty = false;
                        root.ui.runtimeCommand("custom_group_preview", [root.baseRevision, JSON.stringify(root.spec()), root.removing ? "delete" : "save", activate.checked && parentGroup.currentIndex > 0 ? "true" : "false"]);
                    }
                }
                ActionButton {
                    width: Math.min(implicitWidth, parent.width)
                    objectName: "custom_group_apply"
                    theme: root.theme
                    text: root.removing ? qsTr("Delete group…") : qsTr("Save and apply…")
                    prominent: true
                    enabled: root.previewReady
                    onClicked: confirmation.open()
                }
            }
            Label {
                visible: !root.dirty && !!root.preview.token
                text: root.removing ? qsTr("Removal validated. Original groups and rules are preserved.") : root.preview.empty ? qsTr("No matching nodes. Adjust sources or filters before applying.") : qsTr("Matched %1 nodes%2").arg(root.previewGroup.count || 0).arg(root.previewGroup.truncated ? qsTr(" (showing first 100)") : "")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: root.preview.empty ? root.theme.warning : root.theme.text
            }
            Label {
                visible: !root.dirty && !root.removing && !!root.preview.token
                text: (root.previewGroup.members || []).join("\n")
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: root.theme.muted
            }
        }
    }
}
