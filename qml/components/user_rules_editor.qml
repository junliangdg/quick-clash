pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
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
    property var globalDraft: []
    property var localDraft: []
    property string baseRevision: ""
    property string baseProfile: ""
    property string proxyDraft: ""
    property bool globalDirty: false
    property bool localDirty: false
    property bool proxyDirty: false
    property bool saving: false
    property int savingScope: 0
    property string savingAttempt: ""
    property int editingIndex: -1
    objectName: "user_rules_editor"
    onVisibleChanged: {
        if (!visible)
            saving = false;
    }

    Component.onCompleted: {
        const draft = ui.userRulesDraft || ({});
        if (draft.baseRevision) {
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
    function replaceRules(value): void {
        if (scope.currentIndex === 0) {
            globalDraft = value;
            globalDirty = true;
        } else {
            localDraft = value;
            localDirty = true;
        }
    }
    function move(index, direction): void {
        const next = copy(currentRules);
        const other = index + direction;
        if (other < 0 || other >= next.length)
            return;
        const item = next[index];
        next[index] = next[other];
        next[other] = item;
        replaceRules(next);
    }
    function edit(index): void {
        editingIndex = index;
        const rule = index < 0 ? ({
                "type": "DOMAIN-SUFFIX",
                "value": "",
                "target": "DIRECT",
                "enabled": true
            }) : currentRules[index];
        ruleType.currentIndex = Math.max(0, types.indexOf(rule.type));
        ruleValue.text = rule.value || "";
        provider.currentIndex = providers.indexOf(rule.value);
        ruleTarget.currentIndex = targets.indexOf(rule.target);
        ruleEnabled.checked = !!rule.enabled;
        ruleDialog.open();
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
                if (root.savingScope === 0)
                    root.globalDirty = false;
                else
                    root.localDirty = false;
                root.proxyDirty = false;
                root.saving = false;
            } else if (root.saving && !root.ui.runtime.busy && root.state.attempt !== root.savingAttempt && root.state.result === "Failed") {
                root.saving = false;
            } else if (!root.globalDirty && !root.localDirty && !root.proxyDirty && !root.saving && root.state.revision !== root.baseRevision) {
                root.loadState();
            }
        }
    }
    ThemedScrollView {
        anchors.fill: parent
        theme: root.theme
        form: rulesForm
        ColumnLayout {
            id: rulesForm
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: root.theme.muted
                text: qsTr("Profile rules run first, then global rules, then the profile's original rules.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: root.theme.muted
                text: qsTr("Applying changes may restart the core and interrupt connections.")
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
                ThemedComboBox {
                    id: scope
                    objectName: "user_rule_scope"
                    theme: root.theme
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Rule scope")
                    model: [qsTr("Global rules"), qsTr("Current profile rules")]
                }
                ActionButton {
                    theme: root.theme
                    objectName: "user_rule_add"
                    text: qsTr("Add rule")
                    enabled: !!root.state.revision && !!root.baseRevision && (scope.currentIndex === 0 || !!root.baseProfile)
                    onClicked: root.edit(-1)
                }
            }
            Label {
                Layout.fillWidth: true
                color: root.theme.muted
                wrapMode: Text.WordWrap
                text: scope.currentIndex === 0 ? qsTr("Applies to all profiles.") : qsTr("Profile: %1").arg(root.state.profileName || qsTr("Select a profile first"))
            }
            Label {
                visible: root.baseProfile !== (root.state.profileId || "")
                Layout.fillWidth: true
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
                visible: root.currentRules.length === 0
                color: root.theme.muted
                text: qsTr("No custom rules in this scope yet.")
            }
            ListView {
                id: list
                objectName: "user_rule_list"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(root.theme.scaledSize(420), Math.max(root.theme.scaledSize(180), contentHeight))
                clip: true
                reuseItems: true
                spacing: 6
                model: root.currentRules
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property var modelData
                    width: ListView.view.width
                    height: contents.implicitHeight + 16
                    color: root.theme.inset
                    radius: 6
                    ColumnLayout {
                        id: contents
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        Label {
                            Layout.fillWidth: true
                            color: root.theme.text
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: (row.index + 1) + ". " + root.describe(row.modelData)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ThemedSwitch {
                                theme: root.theme
                                text: qsTr("Enabled")
                                checked: !!row.modelData.enabled
                                onClicked: {
                                    const next = root.copy(root.currentRules);
                                    next[row.index].enabled = checked;
                                    root.replaceRules(next);
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: width < root.theme.scaledSize(400) ? 2 : 4
                            ActionButton {
                                Layout.fillWidth: true
                                theme: root.theme
                                text: qsTr("Up")
                                enabled: row.index > 0
                                onClicked: root.move(row.index, -1)
                            }
                            ActionButton {
                                Layout.fillWidth: true
                                theme: root.theme
                                text: qsTr("Down")
                                enabled: row.index + 1 < root.currentRules.length
                                onClicked: root.move(row.index, 1)
                            }
                            ActionButton {
                                Layout.fillWidth: true
                                theme: root.theme
                                text: qsTr("Edit")
                                onClicked: root.edit(row.index)
                            }
                            ActionButton {
                                Layout.fillWidth: true
                                theme: root.theme
                                text: qsTr("Delete")
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
            ColumnLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: root.theme.muted
                    text: root.saving ? qsTr("Applying…") : root.ui.runtime.error ? root.errorText(root.ui.runtime.error) : root.state.result === "Saved" ? (root.ui.runtime.ready ? qsTr("Applied") : qsTr("Saved for next start")) : ""
                }
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Reload")
                        enabled: !root.saving
                        onClicked: {
                            if (root.globalDirty || root.localDirty || root.proxyDirty)
                                reloadDialog.open();
                            else
                                root.loadState();
                        }
                    }
                    ActionButton {
                        theme: root.theme
                        objectName: "user_rule_save"
                        prominent: true
                        text: qsTr("Save and apply")
                        enabled: !!root.state.revision && !root.saving && !root.ui.runtime.busy && !root.ui.runtime.readOnly && root.baseProfile === (root.state.profileId || "") && (root.proxyDirty || (scope.currentIndex === 0 ? root.globalDirty : root.localDirty))
                        onClicked: {
                            root.saving = true;
                            root.savingScope = scope.currentIndex;
                            root.savingAttempt = root.state.attempt || "";
                            root.ui.runtimeCommand("user_rules_save", [root.baseRevision, scope.currentIndex === 0 ? "global" : "local", root.baseProfile, JSON.stringify(root.currentRules), root.proxyDraft]);
                        }
                    }
                }
            }
        }
    }
    ThemedDialog {
        id: reloadDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        theme: root.theme
        modal: true
        title: qsTr("Discard unsaved rule changes?")
        width: Math.min(440, parent.width - 32)
        height: Math.min(240, parent.height - 32)
        implicitHeight: height
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.loadState()
        contentItem: Label {
            text: qsTr("Reloading discards the edits in both rule scopes.")
            wrapMode: Text.WordWrap
            color: root.theme.text
        }
    }
    ThemedDialog {
        id: ruleDialog
        objectName: "user_rule_dialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        theme: root.theme
        modal: true
        title: root.editingIndex < 0 ? qsTr("Add rule") : qsTr("Edit rule")
        width: Math.min(520, parent.width - 32)
        height: Math.min(480, parent.height - 32)
        implicitHeight: height
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            standardButton(Dialog.Ok).enabled = Qt.binding(function () {
                return ruleTarget.currentIndex >= 0 && (ruleType.currentText === "RULE-SET" ? provider.currentIndex >= 0 : ruleValue.text.trim().length > 0);
            });
        }
        onAccepted: {
            const next = root.copy(root.currentRules);
            const old = root.editingIndex < 0 ? ({}) : next[root.editingIndex];
            const value = ruleType.currentText === "RULE-SET" ? provider.currentText : ruleValue.text.trim();
            const rule = {
                "id": old.id || ("rule-" + Date.now() + "-" + Math.random().toString(36).slice(2)),
                "type": ruleType.currentText,
                "value": value,
                "target": ruleTarget.currentText,
                "enabled": ruleEnabled.checked
            };
            if (root.editingIndex < 0)
                next.push(rule);
            else
                next[root.editingIndex] = rule;
            root.replaceRules(next);
        }
        contentItem: ThemedScrollView {
            theme: root.theme
            form: ruleForm
            ColumnLayout {
                id: ruleForm
                spacing: 8
                ThemedComboBox {
                    id: ruleType
                    objectName: "user_rule_type"
                    theme: root.theme
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Match type")
                    model: root.types
                    optionLabels: root.typeLabels
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
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: root.theme.muted
                    text: ruleType.currentText === "DOMAIN-SUFFIX" ? qsTr("Example: example.com matches the domain and its subdomains.") : ruleType.currentText === "IP-CIDR" ? qsTr("Example: 192.0.2.0/24") : ruleType.currentText === "IP-CIDR6" ? qsTr("Example: 2001:db8::/32") : qsTr("Enter a match value, such as example.com or CN.")
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
            }
        }
    }
}
