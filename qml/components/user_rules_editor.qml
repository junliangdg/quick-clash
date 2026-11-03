pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    objectName: "user_rules_editor"
    required property UiState ui
    required property ThemeTokens theme
    readonly property var state: ui.userRules || ({})
    readonly property var groups: state.groups || []
    readonly property var providers: state.providers || []
    readonly property var types: scope.currentIndex === 0 ? ["DOMAIN", "DOMAIN-SUFFIX", "DOMAIN-KEYWORD", "IP-CIDR", "IP-CIDR6", "GEOIP", "GEOSITE"] : ["DOMAIN", "DOMAIN-SUFFIX", "DOMAIN-KEYWORD", "IP-CIDR", "IP-CIDR6", "GEOIP", "GEOSITE", "RULE-SET"]
    readonly property var typeLabels: [qsTr("Exact domain"), qsTr("Domain and subdomains"), qsTr("Domain keyword"), qsTr("IPv4 network"), qsTr("IPv6 network"), qsTr("Geo IP"), qsTr("Geo domain"), qsTr("Existing rule set")]
    readonly property var targets: scope.currentIndex === 0 ? ["DIRECT", "REJECT", "PROXY"] : ["DIRECT", "REJECT"].concat(groups)
    readonly property var targetLabels: scope.currentIndex === 0 ? [qsTr("Direct"), qsTr("Reject"), qsTr("Proxy")] : [qsTr("Direct"), qsTr("Reject")].concat(groups)
    readonly property var currentRules: scope.currentIndex === 0 ? globalDraft : localDraft
    readonly property bool dirty: globalDirty || localDirty || proxyDirty
    readonly property bool canConfirm: !saving && !!state.revision && (!dirty || (!ui.runtime.busy && !ui.runtime.readOnly && baseProfile === (state.profileId || "")))
    property var globalDraft: []
    property var localDraft: []
    property string baseRevision: ""
    property string baseProfile: ""
    property string proxyDraft: ""
    property bool globalDirty: false
    property bool localDirty: false
    property bool proxyDirty: false
    property bool saving: false
    property string savingAttempt: ""
    property int editingIndex: -1
    property bool formOpen: false
    property int draggingIndex: -1
    signal saveCompleted()

    Component.onCompleted: {
        const draft = ui.userRulesDraft || ({});
        if (typeof draft.baseRevision === "string" && draft.baseRevision.length > 0) {
            globalDraft = copy(draft.global);
            localDraft = copy(draft.local);
            baseRevision = draft.baseRevision;
            baseProfile = draft.baseProfile || "";
            proxyDraft = draft.proxy || "";
            globalDirty = !!draft.globalDirty;
            localDirty = !!draft.localDirty;
            proxyDirty = !!draft.proxyDirty;
        }
    }
    Component.onDestruction: {
        ui.userRulesDraft = globalDirty || localDirty || proxyDirty ? {
            "global": globalDraft,
            "local": localDraft,
            "baseRevision": baseRevision,
            "baseProfile": baseProfile,
            "proxy": proxyDraft,
            "globalDirty": globalDirty,
            "localDirty": localDirty,
            "proxyDirty": proxyDirty
        } : ({});
    }
    function copy(value): var {
        return JSON.parse(JSON.stringify(value || []));
    }
    function errorText(code): string {
        switch (code) {
        case "UserRuleConflict":
            return qsTr("The configuration or rules changed. Reload and check your edits.");
        case "InvalidUserRules":
            return qsTr("Check the rule type and match value, including network prefixes.");
        case "UserRuleProxyMissing":
            return qsTr("Choose a proxy group for this profile.");
        case "UserRuleTargetMissing":
            return qsTr("A selected proxy group is no longer available.");
        case "UserRuleProviderMissing":
            return qsTr("A selected rule provider is no longer available.");
        default:
            return code || "";
        }
    }
    function describe(rule): string {
        const typeIndex = types.indexOf(rule.type);
        const targetIndex = targets.indexOf(rule.target);
        return (typeIndex >= 0 ? typeLabels[typeIndex] : rule.type) + " · " + rule.value + " → " + (targetIndex >= 0 ? targetLabels[targetIndex] : rule.target);
    }
    function loadState(): void {
        if (!state.revision)
            return;
        globalDraft = copy(state.global);
        localDraft = copy(state.local);
        proxyDraft = state.proxy || "";
        baseRevision = state.revision;
        baseProfile = state.profileId || "";
        globalDirty = false;
        localDirty = false;
        proxyDirty = false;
    }
    function discardDraft(): void {
        ui.userRulesDraft = ({});
        globalDirty = false;
        localDirty = false;
        proxyDirty = false;
        formOpen = false;
        editingIndex = -1;
        if (state.revision) {
            loadState();
        } else {
            globalDraft = [];
            localDraft = [];
            baseRevision = "";
            baseProfile = "";
            proxyDraft = "";
        }
    }
    function confirm(): void {
        if (!canConfirm)
            return;
        if (!dirty) {
            saveCompleted();
            return;
        }
        saving = true;
        savingAttempt = state.attempt || "";
        ui.runtimeCommand("user_rules_save", [baseRevision, "all", baseProfile,
                                                JSON.stringify({"global": globalDraft, "local": localDraft}),
                                                proxyDraft]);
    }
    function replaceRules(value): void {
        if (scope.currentIndex === 0) {
            globalDraft = value;
            globalDirty = true;
        } else {
            localDraft = value;
            localDirty = true;
        }
    }
    function moveTo(index, targetIndex): void {
        const next = copy(currentRules);
        if (index < 0 || targetIndex < 0 || index >= next.length || targetIndex >= next.length || index === targetIndex)
            return;
        const item = next.splice(index, 1)[0];
        next.splice(targetIndex, 0, item);
        if (draggingIndex === index)
            draggingIndex = targetIndex;
        else if (draggingIndex > index && draggingIndex <= targetIndex)
            draggingIndex -= 1;
        else if (draggingIndex < index && draggingIndex >= targetIndex)
            draggingIndex += 1;
        replaceRules(next);
    }
    function move(index, direction): void {
        moveTo(index, index + direction);
    }
    function dragTo(handle, point): void {
        if (draggingIndex < 0)
            return;
        const location = handle.mapToItem(list, point.x, point.y);
        if (location.y < 0 || location.y >= list.height)
            return;
        const targetIndex = list.indexAt(list.width / 2, location.y);
        if (targetIndex >= 0)
            moveTo(draggingIndex, targetIndex);
    }
    function edit(index): void {
        editingIndex = index;
        const rule = index < 0 ? ({
                "type": "DOMAIN-SUFFIX",
                "value": "",
                "target": "DIRECT",
                "enabled": true
            }) : currentRules[index];
        formOpen = true;
        ruleType.currentIndex = Math.max(0, types.indexOf(rule.type));
        ruleValue.text = rule.value || "";
        provider.currentIndex = providers.indexOf(rule.value);
        ruleTarget.currentIndex = Math.max(0, targets.indexOf(rule.target));
        ruleEnabled.checked = !!rule.enabled;
    }
    function cancelForm(): void {
        formOpen = false;
        editingIndex = -1;
    }
    function saveRuleForm(): void {
        if (ruleTarget.currentIndex < 0 ||
            (ruleType.currentText === "RULE-SET" ? provider.currentIndex < 0 : !ruleValue.text.trim().length))
            return;
        const next = copy(currentRules);
        const old = editingIndex < 0 ? ({}) : next[editingIndex];
        const value = ruleType.currentText === "RULE-SET" ? provider.currentText : ruleValue.text.trim();
        const rule = {
            "id": old.id || ("rule-" + Date.now() + "-" + Math.random().toString(36).slice(2)),
            "type": ruleType.currentText,
            "value": value,
            "target": ruleTarget.currentText,
            "enabled": ruleEnabled.checked
        };
        if (editingIndex < 0)
            next.push(rule);
        else
            next[editingIndex] = rule;
        replaceRules(next);
        formOpen = false;
        editingIndex = -1;
    }

    Connections {
        target: root.ui
        function onUserRulesChanged(): void {
            if (!root.state.revision)
                return;
            if (!root.baseRevision) {
                root.loadState();
            } else if (root.saving && !root.ui.runtime.busy && root.state.attempt !== root.savingAttempt && root.state.result === "Saved") {
                root.baseRevision = root.state.revision;
                root.baseProfile = root.state.profileId || "";
                root.globalDirty = false;
                root.localDirty = false;
                root.proxyDirty = false;
                root.saving = false;
                root.ui.userRulesDraft = ({});
                root.saveCompleted();
            } else if (root.saving && !root.ui.runtime.busy && root.state.attempt !== root.savingAttempt && root.state.result === "Failed") {
                root.saving = false;
            } else if (!root.globalDirty && !root.localDirty && !root.proxyDirty && !root.saving && root.state.revision !== root.baseRevision) {
                root.loadState();
            }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: root.formOpen ? 1 : 0

        ThemedScrollView {
            id: listPage
            objectName: "user_rules_list_page"
            theme: root.theme
            form: listContent
            ColumnLayout {
                id: listContent
                height: Math.max(implicitHeight, listPage.availableHeight - 2 * listPage.focusMargin)
                spacing: 10
                GridLayout {
                    id: toolbar
                    objectName: "user_rules_toolbar"
                    Layout.fillWidth: true
                    Layout.maximumWidth: Number.POSITIVE_INFINITY
                    columns: width >= root.theme.scaledSize(64) + reload.implicitWidth + add.implicitWidth + 16 ? 3 : width >= reload.implicitWidth + add.implicitWidth + 8 ? 2 : 1
                    readonly property real columnItemWidth: columns === 3 ? (width - 2 * columnSpacing) / 3 : columns === 2 ? (width - columnSpacing) / 2 : width
                    columnSpacing: 8
                    rowSpacing: 8
                    ThemedComboBox {
                        id: scope
                        objectName: "user_rule_scope"
                        theme: root.theme
                        implicitWidth: root.theme.scaledSize(80)
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: toolbar.columns === 3 ? toolbar.columnItemWidth : toolbar.width
                        Layout.columnSpan: toolbar.columns === 2 ? 2 : 1
                        Accessible.name: qsTr("Rule scope")
                        model: [qsTr("Global rules"), qsTr("Current profile rules")]
                    }
                    ActionButton {
                        id: reload
                        theme: root.theme
                        objectName: "user_rule_reload"
                        Layout.alignment: Qt.AlignRight
                        Layout.fillWidth: toolbar.columns < 3
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: Math.min(implicitWidth, toolbar.columnItemWidth)
                        Layout.maximumWidth: toolbar.columnItemWidth
                        leftPadding: 8
                        rightPadding: 8
                        text: qsTr("Reload")
                        contentItem: Text {
                            text: reload.text
                            font: reload.font
                            color: reload.foregroundColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        enabled: !root.saving
                        onClicked: {
                            if (root.globalDirty || root.localDirty || root.proxyDirty)
                                reloadDialog.open();
                            else
                                root.loadState();
                        }
                    }
                    ActionButton {
                        id: add
                        theme: root.theme
                        objectName: "user_rule_add"
                        Layout.alignment: Qt.AlignRight
                        Layout.fillWidth: toolbar.columns < 3
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: Math.min(implicitWidth, toolbar.columnItemWidth)
                        Layout.maximumWidth: toolbar.columnItemWidth
                        leftPadding: 8
                        rightPadding: 8
                        text: qsTr("Add rule")
                        contentItem: Text {
                            text: add.text
                            font: add.font
                            color: add.foregroundColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        enabled: !!root.state.revision && !!root.baseRevision && (scope.currentIndex === 0 || !!root.baseProfile)
                        onClicked: root.edit(-1)
                    }
                }
                GridLayout {
                    id: hints
                    objectName: "user_rules_hints"
                    Layout.fillWidth: true
                    columns: width >= scopeHint.implicitWidth + orderHint.implicitWidth + columnSpacing ? 2 : 1
                    columnSpacing: 16
                    rowSpacing: 10
                    Label {
                        id: scopeHint
                        objectName: "user_rules_scope_hint"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: listContent.width
                        wrapMode: Text.WordWrap
                        color: root.theme.muted
                        text: scope.currentIndex === 0 ? qsTr("Applies to all profiles.") : qsTr("Profile: %1").arg(root.state.profileName || qsTr("Select a profile first"))
                    }
                    Label {
                        id: orderHint
                        objectName: "user_rules_order_hint"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: listContent.width
                        wrapMode: Text.WordWrap
                        color: root.theme.muted
                        text: qsTr("Profile rules run first, then global rules, then the profile's original rules.")
                    }
                }
                Label {
                    objectName: "user_rules_mode_hint"
                    visible: (root.ui.runtime.preferences || {}).mode !== "rule"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: listContent.width
                    wrapMode: Text.WordWrap
                    color: root.theme.warning
                    text: qsTr("Custom rules only control traffic in rule mode.")
                }
                Label {
                    visible: root.baseProfile !== (root.state.profileId || "")
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: listContent.width
                    color: root.theme.error
                    wrapMode: Text.WordWrap
                    text: qsTr("The selected profile changed. Reload before saving.")
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !!root.baseProfile && root.globalDraft.some(rule => rule.enabled && rule.target === "PROXY")
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Global proxy group")
                        color: root.theme.text
                    }
                    ThemedComboBox {
                        id: proxyGroup
                        objectName: "user_rule_global_proxy"
                        theme: root.theme
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Global proxy group")
                        model: [qsTr("Choose a group")].concat(root.groups)
                        currentIndex: Math.max(0, root.groups.indexOf(root.proxyDraft) + 1)
                        onActivated: {
                            root.proxyDraft = currentIndex > 0 ? root.groups[currentIndex - 1] : "";
                            root.proxyDirty = true;
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: !!root.proxyDraft && root.groups.indexOf(root.proxyDraft) < 0
                        wrapMode: Text.WordWrap
                        color: root.theme.warning
                        text: qsTr("The previously selected proxy group is unavailable. Choose another group.")
                    }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: listContent.width
                    visible: root.currentRules.length === 0
                    color: root.theme.muted
                    text: qsTr("No custom rules in this scope yet.")
                }
                ListView {
                    id: list
                    objectName: "user_rule_list"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: listContent.width
                    Layout.fillHeight: true
                    Layout.minimumHeight: root.theme.scaledSize(80)
                    Layout.preferredHeight: root.theme.scaledSize(120)
                    clip: true
                    reuseItems: true
                    spacing: 4
                    model: root.currentRules
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property var modelData
                        objectName: "user_rule_row_" + index
                        width: ListView.view.width
                        height: contents.implicitHeight + 8
                        color: root.theme.inset
                        radius: 6
                        opacity: root.draggingIndex === row.index ? 0.82 : 1
                        border.width: root.draggingIndex === row.index ? 1 : 0
                        border.color: root.theme.accent
                        GridLayout {
                            id: contents
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.topMargin: 4
                            anchors.bottomMargin: 4
                            columns: row.width >= root.theme.scaledSize(500) ? 2 : 1
                            columnSpacing: 6
                            rowSpacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 6
                                Item {
                                    id: dragHandle
                                    objectName: "user_rule_drag_" + row.index
                                    Layout.preferredWidth: root.theme.scaledSize(root.width < root.theme.scaledSize(500) ? 18 : 24)
                                    Layout.fillHeight: true
                                    Accessible.role: Accessible.Button
                                    Accessible.name: qsTr("Drag to reorder")
                                    HoverHandler { cursorShape: Qt.SizeVerCursor }
                                    Text {
                                        anchors.centerIn: parent
                                        text: "⋮⋮"
                                        color: root.theme.accent
                                        font.pixelSize: root.theme.scaledSize(18)
                                    }
                                    DragHandler {
                                        target: null
                                        xAxis.enabled: false
                                        yAxis.enabled: true
                                        onActiveChanged: root.draggingIndex = active ? row.index : -1
                                        onCentroidChanged: if (active) root.dragTo(dragHandle, centroid.position)
                                    }
                                }
                                Label {
                                    text: "#" + (row.index + 1)
                                    color: root.theme.accent
                                    font.weight: Font.DemiBold
                                    visible: root.width >= root.theme.scaledSize(500)
                                    Layout.preferredWidth: root.theme.scaledSize(28)
                                }
                                ColumnLayout {
                                    objectName: "user_rule_description_" + row.index
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 2
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        color: root.theme.text
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        textFormat: Text.PlainText
                                        text: row.modelData.type || ""
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        color: root.theme.text
                                        elide: Text.ElideRight
                                        textFormat: Text.PlainText
                                        text: (row.modelData.value || "") + "  →  " + (row.modelData.target || "")
                                    }
                                }
                                ThemedSwitch {
                                    theme: root.theme
                                    objectName: "user_rule_enabled_" + row.index
                                    text: root.width < root.theme.scaledSize(620) ? "" : qsTr("Enabled")
                                    Accessible.name: qsTr("Enabled")
                                    checked: !!row.modelData.enabled
                                    onClicked: {
                                        const next = root.copy(root.currentRules);
                                        next[row.index].enabled = checked;
                                        root.replaceRules(next);
                                    }
                                }
                            }
                            GridLayout {
                                Layout.fillWidth: contents.columns === 1
                                Layout.alignment: Qt.AlignRight
                                columns: row.width - 16 >= 4 * root.theme.scaledSize(28) + 12 ? 4 : row.width - 16 >= 2 * root.theme.scaledSize(28) + 4 ? 2 : 1
                                columnSpacing: 4
                                rowSpacing: 4
                                ActionButton {
                                    theme: root.theme
                                    objectName: "user_rule_up_" + row.index
                                    text: "↑"
                                    Accessible.name: qsTr("Move up")
                                    Layout.preferredWidth: root.theme.scaledSize(root.width < root.theme.scaledSize(500) ? 28 : 34)
                                    leftPadding: 2
                                    rightPadding: 2
                                    font.pixelSize: root.theme.scaledSize(16)
                                    enabled: row.index > 0
                                    onClicked: root.move(row.index, -1)
                                }
                                ActionButton {
                                    theme: root.theme
                                    objectName: "user_rule_down_" + row.index
                                    text: "↓"
                                    Accessible.name: qsTr("Move down")
                                    Layout.preferredWidth: root.theme.scaledSize(root.width < root.theme.scaledSize(500) ? 28 : 34)
                                    leftPadding: 2
                                    rightPadding: 2
                                    font.pixelSize: root.theme.scaledSize(16)
                                    enabled: row.index + 1 < root.currentRules.length
                                    onClicked: root.move(row.index, 1)
                                }
                                ActionButton {
                                    theme: root.theme
                                    objectName: "user_rule_edit_" + row.index
                                    text: root.width < root.theme.scaledSize(500) ? "✎" : qsTr("Edit")
                                    Accessible.name: qsTr("Edit rule")
                                    Layout.preferredWidth: root.theme.scaledSize(root.width < root.theme.scaledSize(500) ? 28 : 54)
                                    leftPadding: 2
                                    rightPadding: 2
                                    font.pixelSize: root.theme.supportingSize
                                    onClicked: root.edit(row.index)
                                }
                                ActionButton {
                                    theme: root.theme
                                    objectName: "user_rule_delete_" + row.index
                                    text: root.width < root.theme.scaledSize(500) ? "×" : qsTr("Delete")
                                    Accessible.name: qsTr("Delete rule")
                                    destructive: true
                                    Layout.preferredWidth: root.theme.scaledSize(root.width < root.theme.scaledSize(500) ? 28 : 58)
                                    leftPadding: 2
                                    rightPadding: 2
                                    font.pixelSize: root.theme.supportingSize
                                    onClicked: {
                                        const next = root.copy(root.currentRules);
                                        next.splice(row.index, 1);
                                        root.replaceRules(next);
                                    }
                                }
                            }
                        }
                    }
                }
                Label {
                    objectName: "user_rules_reorder_hint"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: listContent.width
                    wrapMode: Text.WordWrap
                    color: root.theme.muted
                    text: qsTr("Drag the handle to reorder. The move buttons also support keyboard use.")
                }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: listContent.width
                    visible: text.length > 0
                    wrapMode: Text.WordWrap
                    color: root.saving ? root.theme.muted : root.ui.runtime.error ? root.theme.error : root.theme.success
                    text: root.saving ? qsTr("Applying…") : root.ui.runtime.error ? root.errorText(root.ui.runtime.error) : root.state.result === "Saved" ? (root.ui.runtime.ready ? qsTr("Applied") : qsTr("Saved for next start")) : ""
                }
            }
        }

        ThemedScrollView {
            theme: root.theme
            form: editForm
            ColumnLayout {
                id: editForm
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Back")
                        onClicked: root.cancelForm()
                    }
                    Label {
                        text: root.editingIndex < 0 ? qsTr("Add rule") : qsTr("Edit rule")
                        color: root.theme.text
                        font.pixelSize: root.theme.scaledSize(16)
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: root.theme.muted
                    text: scope.currentIndex === 0 ? qsTr("Applies to all profiles.") : qsTr("Profile: %1").arg(root.state.profileName || qsTr("Select a profile first"))
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Match type")
                    color: root.theme.text
                }
                ThemedComboBox {
                    id: ruleType
                    objectName: "user_rule_type"
                    theme: root.theme
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Match type")
                    model: root.types
                    optionLabels: root.typeLabels
                }
                Label {
                    visible: ruleType.currentText !== "RULE-SET"
                    Layout.fillWidth: true
                    text: qsTr("Match value")
                    color: root.theme.text
                }
                ThemedField {
                    id: ruleValue
                    objectName: "user_rule_value"
                    theme: root.theme
                    Layout.fillWidth: true
                    visible: ruleType.currentText !== "RULE-SET"
                    Accessible.name: qsTr("Match value")
                    placeholderText: qsTr("Domain, keyword, network or Geo code")
                }
                Label {
                    visible: ruleType.currentText !== "RULE-SET"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: root.theme.muted
                    text: ruleType.currentText === "DOMAIN-SUFFIX" ? qsTr("Example: example.com matches the domain and its subdomains.") : ruleType.currentText === "IP-CIDR" ? qsTr("Example: 192.0.2.0/24") : ruleType.currentText === "IP-CIDR6" ? qsTr("Example: 2001:db8::/32") : qsTr("Enter a match value, such as example.com or CN.")
                }
                Label {
                    visible: ruleType.currentText === "RULE-SET"
                    Layout.fillWidth: true
                    text: qsTr("Rule provider")
                    color: root.theme.text
                }
                ThemedComboBox {
                    id: provider
                    objectName: "user_rule_provider"
                    theme: root.theme
                    Layout.fillWidth: true
                    visible: ruleType.currentText === "RULE-SET"
                    Accessible.name: qsTr("Rule provider")
                    model: root.providers
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Rule target")
                    color: root.theme.text
                }
                ThemedComboBox {
                    id: ruleTarget
                    objectName: "user_rule_target"
                    theme: root.theme
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Rule target")
                    model: root.targets
                    optionLabels: root.targetLabels
                }
                ThemedSwitch {
                    id: ruleEnabled
                    theme: root.theme
                    text: qsTr("Enabled")
                }
                Label {
                    visible: (root.ui.runtime.preferences || {}).mode !== "rule"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: root.theme.warning
                    text: qsTr("Custom rules only control traffic in rule mode.")
                }
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Cancel")
                        onClicked: root.cancelForm()
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        theme: root.theme
                        objectName: "user_rule_form_save"
                        prominent: true
                        text: qsTr("Save rule")
                        enabled: ruleTarget.currentIndex >= 0 && (ruleType.currentText === "RULE-SET" ? provider.currentIndex >= 0 : ruleValue.text.trim().length > 0)
                        onClicked: root.saveRuleForm()
                    }
                }
            }
        }
    }

    ThemedDialog {
        id: reloadDialog
        objectName: "user_rule_reload_dialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        theme: root.theme
        modal: true
        title: qsTr("Discard unsaved rule changes?")
        width: Math.min(root.theme.scaledSize(440), parent.width - root.theme.scaledSize(32))
        height: Math.min(root.theme.scaledSize(240), parent.height - root.theme.scaledSize(32))
        implicitHeight: height
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.loadState()
        contentItem: Label {
            text: qsTr("Reloading discards the edits in both rule scopes.")
            wrapMode: Text.WordWrap
            color: root.theme.text
        }
    }
}
