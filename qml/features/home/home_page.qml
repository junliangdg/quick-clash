pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ScrollView {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    property bool active: ui.uiVisible && visible
    readonly property var selectedProfile: (ui.runtime.profiles || []).find(profile => profile.id === ui.runtime.selectedId) || ({})
    clip: true
    contentWidth: availableWidth
    function syncDemand(): void {
        ui.setDemand("home:traffic", "traffic", active);
        ui.setDemand("home:resources", "resources", active);
    }
    onActiveChanged: syncDemand()
    Component.onCompleted: syncDemand()
    Component.onDestruction: {
        ui.setDemand("home:traffic", "traffic", false);
        ui.setDemand("home:resources", "resources", false);
    }
    ColumnLayout {
        width: root.availableWidth
        spacing: 14
        GridLayout {
            columns: root.availableWidth >= 760 ? 4 : 2
            columnSpacing: 14
            rowSpacing: 14
            Layout.fillWidth: true
            FlatTile {
                theme: root.theme
                Layout.columnSpan: 2
                Layout.fillWidth: true
                implicitHeight: statusContent.implicitHeight + 40
                ColumnLayout {
                    id: statusContent
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10
                    Label {
                        font.family: root.theme.fontFamily
                        text: root.ui.replay ? qsTr("Replay mode · Proxy stopped") : root.ui.runtimeStatus
                        color: root.theme.success
                        font.bold: true
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: root.ui.replay ? qsTr("Example subscription / Sample node") : (root.ui.runtime.selectedName || qsTr("Start with a subscription"))
                        color: root.theme.text
                        font.pixelSize: 23
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: root.ui.runtimeError || (root.ui.runtime.ready ? qsTr("Official Mihomo %1 · Local proxy ready").arg(root.ui.runtime.coreVersion) : qsTr("The network engine runs automatically after a valid profile is selected."))
                        color: root.theme.muted
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        ActionButton {
                            theme: root.theme
                            text: qsTr("Manage subscriptions")
                            prominent: true
                            onClicked: root.ui.navigate("subscriptions")
                        }
                        ActionButton {
                            theme: root.theme
                            text: root.ui.replay ? qsTr("End replay") : qsTr("Preview replay")
                            onClicked: root.ui.replay = !root.ui.replay
                        }
                    }
                    SubscriptionDetails {
                        visible: !!root.ui.runtime.selectedId && !root.ui.replay
                        Layout.fillWidth: true
                        theme: root.theme
                        profile: root.selectedProfile
                    }
                }
            }
            Repeater {
                model: [
                    {
                        "label": qsTr("Upload"),
                        "up": true
                    },
                    {
                        "label": qsTr("Download"),
                        "up": false
                    }
                ]
                delegate: FlatTile {
                    id: rateTile
                    required property var modelData
                    theme: root.theme
                    Layout.fillWidth: true
                    implicitHeight: 182
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        Label {
                            font.family: root.theme.fontFamily
                            text: rateTile.modelData.label
                            color: root.theme.muted
                        }
                        Label {
                            font.family: root.theme.fontFamily
                            text: rateTile.modelData.up ? root.ui.upload : root.ui.download
                            color: root.theme.accent
                            font.pixelSize: 23
                            font.bold: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Label {
                            font.family: root.theme.fontFamily
                            text: root.ui.replay ? qsTr("Synthetic data · 1 Hz") : ((root.ui.runtime.features || {}).traffic ? qsTr("Live core traffic · 1 Hz") : qsTr("Waiting for core data"))
                            color: root.theme.muted
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Item {
                            Layout.fillHeight: true
                        }
                        TrafficSparkline {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            samples: rateTile.modelData.up ? root.ui.uploadHistory : root.ui.downloadHistory
                            lineColor: root.theme.accent
                            fillColor: root.theme.accentSurface
                        }
                    }
                }
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: controls.childrenRect.height + 28
            Flow {
                id: controls
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("Outbound mode")
                    color: root.theme.text
                    height: 36
                    verticalAlignment: Text.AlignVCenter
                }
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
                        text: modelData.label
                        enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy
                        prominent: !!root.ui.runtime.preferences && root.ui.runtime.preferences.mode === modelData.key
                        onClicked: root.ui.runtimeCommand("mode", [modelData.key])
                    }
                }
                Switch {
                    font.family: root.theme.fontFamily
                    text: qsTr("System proxy")
                    enabled: !!root.ui.runtime.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly
                    checked: !!root.ui.runtime.systemProxy || !!(root.ui.runtime.preferences || {}).systemProxyDesired
                    onClicked: root.ui.runtimeCommand("system_proxy", [checked ? "true" : "false"])
                    palette.windowText: root.theme.muted
                }
                Switch {
                    font.family: root.theme.fontFamily
                    text: qsTr("Virtual network adapter (TUN)") + " · " + (root.ui.runtime.tunState === "On" ? qsTr("On") : root.ui.runtime.tunState === "AuthorizationRequired" ? qsTr("Service required") : qsTr("Off"))
                    checked: root.ui.runtime.tunState === "On" || !!(root.ui.runtime.preferences || {}).tunDesired
                    enabled: !root.ui.replay && !root.ui.runtime.busy && !!root.ui.runtime.selectedId
                    onClicked: root.ui.runtimeCommand("tun", [checked ? "true" : "false"])
                    palette.windowText: root.theme.muted
                }
            }
        }
        GridLayout {
            columns: root.availableWidth >= 650 ? 2 : 1
            columnSpacing: 14
            rowSpacing: 14
            Layout.fillWidth: true
            FlatTile {
                theme: root.theme
                Layout.fillWidth: true
                implicitHeight: networkContent.implicitHeight + 36
                ColumnLayout {
                    id: networkContent
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10
                    Label {
                        font.family: root.theme.fontFamily
                        text: qsTr("Subscription and network")
                        font.bold: true
                        color: root.theme.text
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: root.ui.replay ? qsTr("Example subscription · Usage and expiry unknown") : qsTr("%1 profiles · %2").arg((root.ui.runtime.profiles || []).length).arg(root.ui.runtime.ready ? qsTr("Core ready") : qsTr("Core stopped"))
                        color: root.theme.muted
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: qsTr("IP information not queried. Choose a website and run a test manually.")
                        color: root.theme.muted
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Website test")
                        onClicked: root.ui.navigate("tests")
                    }
                }
            }
            EmbeddedHost {
                ui: root.ui
                theme: root.theme
                contentId: "proxy_summary"
                instanceKey: "home:proxy_summary"
                Layout.fillWidth: true
                Layout.preferredHeight: 170
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: info.implicitHeight + 36
            ColumnLayout {
                id: info
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        font.family: root.theme.fontFamily
                        text: qsTr("Connections and system")
                        font.bold: true
                        color: root.theme.text
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("View connections")
                        onClicked: root.ui.navigate("connections")
                    }
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: {
                        const mem = (root.ui.runtime.features || {}).memory || {};
                        const client = mem.client || {}, core = mem.core || {};
                        return qsTr("Client / core private working set: %1 / %2 MB").arg(client.privateWorkingSet ? (Number(client.privateWorkingSet) / 1000000).toFixed(1) : "—").arg(core.privateWorkingSet ? (Number(core.privateWorkingSet) / 1000000).toFixed(1) : "—") + "\n" + qsTr("Working set: %1 / %2 MB · Private commit: %3 / %4 MB").arg(client.workingSet ? (Number(client.workingSet) / 1000000).toFixed(1) : "—").arg(core.workingSet ? (Number(core.workingSet) / 1000000).toFixed(1) : "—").arg(client.privateCommit ? (Number(client.privateCommit) / 1000000).toFixed(1) : "—").arg(core.privateCommit ? (Number(core.privateCommit) / 1000000).toFixed(1) : "—") + "\n" + ((root.ui.runtime.features || {}).memorySample || "") + "\n" + (root.ui.runtime.platform || "") + " · " + (root.ui.runtime.architecture || "") + " · Mihomo " + (root.ui.runtime.coreVersion || "—");
                    }
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    font.family: root.theme.fontFamily
                    text: qsTr("Active views %1 · Shared streams %2").arg(root.ui.activeDemandCount).arg(root.ui.activeStreamCount)
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: qsTr("Core uptime: %1 s").arg(root.ui.runtime.coreUptimeSeconds || 0) +
                          ((root.ui.runtime.features || {}).memoryComplete === false ? " · " + qsTr("Resource sample is incomplete") : "")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
