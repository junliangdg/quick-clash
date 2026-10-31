pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ColumnLayout {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    readonly property var outbound: ui.runtime.outbound || ({})
    readonly property var features: ui.runtime.features || ({})
    readonly property var primary: {
        for (const group of ui.groups)
            if (group.name === outbound.policy)
                return group;
        return ({});
    }
    readonly property bool tunOn: ui.runtime.tunState === "On"
    spacing: 12
    RowLayout {
        Layout.fillWidth: true
        DashboardHeading {
            theme: root.theme
            source: "qrc:/icons/navigation/proxies.svg"
            text: qsTr("Proxy and network")
            Layout.fillWidth: true
        }
        ActionButton {
            theme: root.theme
            text: qsTr("View proxies")
            ghost: true
            onClicked: root.ui.navigate("proxies", {
                group: root.outbound.policy || "primary"
            })
        }
    }
    GridLayout {
        id: sections
        objectName: "home_proxy_sections"
        Layout.fillWidth: true
        columns: width >= 960 ? 2 : 1
        columnSpacing: 24
        rowSpacing: 12
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 400
            Layout.alignment: Qt.AlignTop
            spacing: 10
            Flow {
                Layout.fillWidth: true
                spacing: 8
                ColumnLayout {
                    spacing: 6
                    Label {
                        text: qsTr("Outbound mode")
                        color: root.theme.muted
                    }
                    RowLayout {
                        spacing: 4
                        Repeater {
                            model: [
                                {
                                    label: qsTr("Rule"),
                                    key: "rule"
                                },
                                {
                                    label: qsTr("Global"),
                                    key: "global"
                                },
                                {
                                    label: qsTr("Direct"),
                                    key: "direct"
                                }
                            ]
                            delegate: ActionButton {
                                required property var modelData
                                theme: root.theme
                                objectName: "home_mode_" + modelData.key
                                text: modelData.label
                                enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly
                                selected: ((root.ui.runtime.settingsState || {}).running ? ((root.ui.runtime.settingsState || {}).effective || {}) : (root.ui.runtime.preferences || {})).mode === modelData.key
                                onClicked: root.ui.runtimeCommand("mode", [modelData.key])
                            }
                        }
                    }
                }
                ThemedSwitch {
                    theme: root.theme
                    objectName: "home_system_proxy"
                    text: qsTr("System proxy") + " · " + (root.ui.runtime.systemProxy ? qsTr("On") : qsTr("Off"))
                    enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly && (root.ui.runtime.capabilities || {}).systemProxy !== "Unsupported"
                    checked: !!root.ui.runtime.systemProxy
                    onClicked: root.ui.runtimeCommand("system_proxy", [checked ? "true" : "false"])
                }
                ThemedSwitch {
                    theme: root.theme
                    objectName: "home_tun"
                    text: "TUN · " + (root.ui.runtime.tunPending ? qsTr("Starting") : root.ui.runtime.tunDisabling ? qsTr("Stopping") : root.tunOn ? qsTr("On") : root.ui.runtime.tunState === "AuthorizationRequired" ? qsTr("Service required") : ["Unknown", "Missing", "Blocked", "Uncertain"].includes(root.ui.runtime.tunState) ? qsTr("Unavailable") : qsTr("Off"))
                    checked: root.ui.runtime.tunPending === true || (!root.ui.runtime.tunDisabling && root.tunOn)
                    enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly && (root.ui.runtime.capabilities || {}).tun !== "Unsupported"
                    onClicked: root.ui.runtimeCommand("tun", [checked ? "true" : "false"])
                }
            }
            Label {
                text: root.tunOn ? qsTr("TUN handles traffic according to system routes.") : root.ui.runtime.systemProxy ? qsTr("System proxy handles applications that follow system proxy settings.") : qsTr("Network takeover is off. The local proxy port remains available when the core is ready.")
                color: root.theme.muted
                font.pixelSize: root.theme.supportingSize
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Label {
                visible: (root.ui.runtime.capabilities || {}).systemProxy === "Unsupported" || (root.ui.runtime.capabilities || {}).tun === "Unsupported"
                text: qsTr("System proxy and TUN are not available on this platform yet.")
                color: root.theme.muted
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 520
            Layout.alignment: Qt.AlignTop
            spacing: 8
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: root.theme.border
                visible: sections.columns === 1
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                UiIcon {
                    theme: root.theme
                    source: "qrc:/icons/navigation/proxies.svg"
                    color: root.theme.info
                    Layout.alignment: Qt.AlignTop
                }
                OutboundStatus {
                    ui: root.ui
                    theme: root.theme
                    color: root.outbound.confirmed ? root.theme.text : root.theme.muted
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
            }
            ThemedComboBox {
                theme: root.theme
                objectName: "home_proxy_selector"
                Accessible.name: qsTr("Primary group member")
                Layout.fillWidth: true
                visible: root.primary.canSelect === true && root.outbound.mode !== "direct"
                enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy && !root.features.groupsStale
                model: root.primary.members || []
                currentIndex: (root.primary.members || []).indexOf(root.primary.current)
                onActivated: root.ui.runtimeCommand("proxy", [root.primary.name, currentText])
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                RowLayout {
                    visible: !!root.outbound.node && !["DIRECT", "REJECT"].includes(root.outbound.node)
                    height: delayAction.height
                    spacing: 6
                    UiIcon {
                        theme: root.theme
                        source: "qrc:/icons/dashboard/gauge.svg"
                        color: delayValue.color
                    }
                    Label {
                        id: delayValue
                        objectName: "home_delay_value"
                        readonly property string result: root.ui.delays[root.outbound.node] || ""
                        text: result === "Timeout" ? qsTr("Timed out") : result === "Failed" ? qsTr("Unavailable") : result === "Testing" ? qsTr("Testing…") : result === "Queued" ? qsTr("Queued") : result === "Cancelled" ? qsTr("Cancelled") : result === "Stale" ? qsTr("Expired") : result || qsTr("Not tested")
                        color: root.features.groupsStale ? root.theme.muted : root.theme.delayColor(result)
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: root.features.delayTimes && root.features.delayTimes[root.outbound.node] ? new Date(root.features.delayTimes[root.outbound.node]).toLocaleTimeString(Qt.locale()) : ""
                        color: root.theme.muted
                        font.pixelSize: root.theme.supportingSize
                    }
                }
                ActionButton {
                    id: delayAction
                    theme: root.theme
                    text: qsTr("Test current node")
                    icon.source: "qrc:/icons/dashboard/gauge.svg"
                    visible: !!root.outbound.node && !["DIRECT", "REJECT"].includes(root.outbound.node)
                    enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy && !root.features.groupsStale && (root.features.delayTest || {}).state !== "Running"
                    onClicked: root.ui.runtimeCommand("delay", [root.outbound.node])
                }
                ActionButton {
                    theme: root.theme
                    text: qsTr("Refresh groups")
                    visible: !!root.features.groupsStale
                    enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy
                    onClicked: root.ui.runtimeCommand("refresh_groups")
                }
            }
            Label {
                text: root.features.groupsStale ? qsTr("Last confirmed policy · waiting for refresh") : qsTr("Other rules and existing connections may use different outbound nodes.")
                color: root.features.groupsStale ? root.theme.warning : root.theme.muted
                font.pixelSize: root.theme.supportingSize
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
    }
}
