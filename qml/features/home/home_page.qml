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
    readonly property string selectedId: ui.runtime.selectedId || ""
    property var selectedProfile: ui.profileModel.profile(selectedId)
    onSelectedIdChanged: selectedProfile = ui.profileModel.profile(selectedId)
    Connections {
        target: root.ui.profileModel
        function onProfileChanged(profileId: string) {
            if (profileId === root.ui.runtime.selectedId)
                root.selectedProfile = root.ui.profileModel.profile(profileId);
        }
    }
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
        spacing: root.theme.gap
        GridLayout {
            columns: root.availableWidth >= 600 ? 2 : 1
            columnSpacing: root.theme.gap
            rowSpacing: root.theme.gap
            Layout.fillWidth: true
            FlatTile {
                objectName: "current_subscription_card"
                theme: root.theme
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                implicitHeight: Math.max(224, statusContent.implicitHeight + 32)
                ColumnLayout {
                    id: statusContent
                    anchors.fill: parent
                    anchors.margins: root.theme.panelPadding
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: "▤"
                            color: root.theme.muted
                            font.pixelSize: root.theme.scaledSize(17)
                        }
                        Label {
                            text: qsTr("Current subscription")
                            color: root.theme.muted
                            font.pixelSize: root.theme.scaledSize(12)
                            Layout.fillWidth: true
                        }
                        Rectangle {
                            visible: !!root.selectedId
                            implicitWidth: activeProfileText.implicitWidth + 12
                            implicitHeight: 22
                            radius: 4
                            color: root.theme.accentSurface
                            Label {
                                id: activeProfileText
                                anchors.centerIn: parent
                                text: qsTr("Active")
                                color: root.theme.onSelection
                                font.pixelSize: root.theme.scaledSize(12)
                            }
                        }
                    }
                    Label {
                        text: root.ui.runtime.selectedName || qsTr("Start with a subscription")
                        textFormat: Text.PlainText
                        color: root.theme.text
                        font.pixelSize: root.theme.scaledSize(20)
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        text: qsTr("%1 proxy groups · %2 nodes").arg(root.ui.groups.length).arg(root.ui.nodes.length)
                        color: root.theme.muted
                        font.pixelSize: root.theme.scaledSize(12)
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    SubscriptionDetails {
                        visible: !!root.selectedId
                        Layout.fillWidth: true
                        theme: root.theme
                        profile: root.selectedProfile
                        overview: true
                    }
                    Item {
                        Layout.fillHeight: true
                    }
                    ActionButton {
                        theme: root.theme
                        text: qsTr("Add subscription")
                        prominent: true
                        visible: !root.selectedId
                        onClicked: root.ui.navigate("subscriptions")
                    }
                }
            }
            FlatTile {
                objectName: "traffic_card"
                theme: root.theme
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                implicitHeight: Math.max(224, trafficContent.implicitHeight + 32)
                ColumnLayout {
                    id: trafficContent
                    anchors.fill: parent
                    anchors.margins: root.theme.panelPadding
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: "⌁"
                            color: root.theme.muted
                            font.pixelSize: root.theme.scaledSize(17)
                        }
                        Label {
                            text: qsTr("Network traffic")
                            color: root.theme.muted
                            font.pixelSize: root.theme.scaledSize(12)
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("Last 60 seconds")
                            color: root.theme.muted
                            font.pixelSize: root.theme.scaledSize(12)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        ColumnLayout {
                            spacing: 4
                            RowLayout {
                                spacing: 6
                                Rectangle {
                                    implicitWidth: 6
                                    implicitHeight: 6
                                    radius: 3
                                    color: root.theme.download
                                }
                                Label {
                                    text: qsTr("Download")
                                    color: root.theme.muted
                                    font.pixelSize: root.theme.scaledSize(12)
                                }
                            }
                            RowLayout {
                                spacing: 6
                                Rectangle {
                                    implicitWidth: 6
                                    implicitHeight: 6
                                    radius: 3
                                    color: root.theme.upload
                                }
                                Label {
                                    text: qsTr("Upload")
                                    color: root.theme.muted
                                    font.pixelSize: root.theme.scaledSize(12)
                                }
                            }
                        }
                        Repeater {
                            model: [root.ui.download, root.ui.upload]
                            delegate: RowLayout {
                                id: rateMetric
                                required property string modelData
                                readonly property int unitOffset: modelData.lastIndexOf(" ")
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 4
                                Item {
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: rateMetric.unitOffset < 0 ? rateMetric.modelData : rateMetric.modelData.slice(0, rateMetric.unitOffset)
                                    color: root.theme.text
                                    font.pixelSize: root.theme.scaledSize(24)
                                    font.weight: Font.DemiBold
                                    Layout.minimumWidth: 0
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: rateMetric.unitOffset < 0 ? "" : rateMetric.modelData.slice(rateMetric.unitOffset + 1)
                                    color: root.theme.muted
                                    font.pixelSize: root.theme.scaledSize(12)
                                    Layout.alignment: Qt.AlignBottom
                                    Layout.bottomMargin: 4
                                }
                            }
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 76
                        TrafficSparkline {
                            objectName: "traffic_chart"
                            anchors.fill: parent
                            uploadSamples: root.ui.uploadHistory
                            downloadSamples: root.ui.downloadHistory
                            uploadColor: root.theme.upload
                            downloadColor: root.theme.download
                            gridColor: root.theme.border
                            uploadFillOpacity: 0.06
                            downloadFillOpacity: 0.10
                            visible: root.active
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: !root.ui.replay && !root.ui.telemetry.traffic
                            text: qsTr("Waiting for core data")
                            color: root.theme.muted
                            font.pixelSize: root.theme.scaledSize(12)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("60 s ago")
                            color: root.theme.muted
                            font.pixelSize: root.theme.scaledSize(12)
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("Now")
                            color: root.theme.muted
                            font.pixelSize: root.theme.scaledSize(12)
                        }
                    }
                }
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: controls.implicitHeight + 32
            GridLayout {
                id: controls
                anchors.fill: parent
                anchors.margins: root.theme.panelPadding
                columns: root.availableWidth >= 700 ? 3 : 1
                columnSpacing: 24
                rowSpacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        text: qsTr("Outbound mode")
                        color: root.theme.text
                        font.weight: Font.DemiBold
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
                                objectName: "home_mode_" + modelData.key
                                theme: root.theme
                                text: modelData.label
                                enabled: !!root.selectedId && !root.ui.runtime.busy
                                selected: ((root.ui.runtime.settingsState || {}).running ? ((root.ui.runtime.settingsState || {}).effective || {}) : (root.ui.runtime.preferences || {})).mode === modelData.key
                                onClicked: root.ui.runtimeCommand("mode", [modelData.key])
                            }
                        }
                    }
                }
                ThemedSwitch {
                    theme: root.theme
                    Layout.fillWidth: true
                    text: qsTr("System proxy")
                    objectName: "home_system_proxy"
                    enabled: !!root.selectedId && !root.ui.runtime.busy && !root.ui.runtime.readOnly && (root.ui.runtime.capabilities || {}).systemProxy !== "Unsupported"
                    checked: !!root.ui.runtime.systemProxy
                    onClicked: root.ui.runtimeCommand("system_proxy", [checked ? "true" : "false"])
                }
                ThemedSwitch {
                    theme: root.theme
                    Layout.fillWidth: true
                    text: qsTr("Virtual network adapter (TUN)") + " · " + (root.ui.runtime.tunState === "On" ? qsTr("On") : root.ui.runtime.tunState === "AuthorizationRequired" ? qsTr("Service required") : qsTr("Off"))
                    objectName: "home_tun"
                    checked: root.ui.runtime.tunState === "On"
                    enabled: !root.ui.runtime.busy && !!root.selectedId && (root.ui.runtime.capabilities || {}).tun !== "Unsupported"
                    onClicked: root.ui.runtimeCommand("tun", [checked ? "true" : "false"])
                }
                Label {
                    visible: (root.ui.runtime.capabilities || {}).systemProxy === "Unsupported" || (root.ui.runtime.capabilities || {}).tun === "Unsupported"
                    text: qsTr("System proxy and TUN are not available on this platform yet.")
                    color: root.theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
        EmbeddedHost {
            ui: root.ui
            theme: root.theme
            contentId: "proxy_summary"
            instanceKey: "home:proxy_summary"
            Layout.fillWidth: true
            Layout.preferredHeight: 140
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: tools.implicitHeight + 32
            ColumnLayout {
                id: tools
                anchors.fill: parent
                anchors.margins: root.theme.panelPadding
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Network tools")
                        font.weight: Font.DemiBold
                        color: root.theme.text
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        theme: root.theme
                        ghost: true
                        text: qsTr("Website test")
                        onClicked: root.ui.navigate("tests")
                    }
                }
                Label {
                    text: qsTr("IP information not queried. Choose a website and run a test manually.")
                    color: root.theme.muted
                    font.pixelSize: root.theme.scaledSize(12)
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
        FlatTile {
            theme: root.theme
            Layout.fillWidth: true
            implicitHeight: info.implicitHeight + 32
            ColumnLayout {
                id: info
                anchors.fill: parent
                anchors.margins: root.theme.panelPadding
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        font.family: root.theme.fontFamily
                        text: qsTr("Connections and system")
                        font.weight: Font.DemiBold
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
                    text: root.ui.runtime.ready ? qsTr("Official Clash Meta (mihomo) %1 · Local proxy ready").arg(root.ui.runtime.coreVersion) : qsTr("The network engine runs automatically after a valid profile is selected.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: width >= 600 ? 2 : 1
                    columnSpacing: 24
                    rowSpacing: 12
                    Repeater {
                        model: ["client", "core", "validator", "serviceValidator"]
                        delegate: ColumnLayout {
                            id: memoryColumn
                            required property string modelData
                            readonly property var counters: (root.ui.telemetry.memory || {})[modelData] || ({})
                            function megabytes(value): string {
                                return value !== undefined ? (Number(value) / 1000000).toFixed(1) : "—";
                            }
                            Layout.fillWidth: true
                            visible: modelData === "client" || modelData === "core" || Object.keys(counters).length > 0
                            Label {
                                text: memoryColumn.modelData === "client" ? qsTr("QuickClash · Client") : memoryColumn.modelData === "validator" || memoryColumn.modelData === "serviceValidator" ? qsTr("Clash Meta (mihomo) · Configuration validator") : qsTr("Clash Meta (mihomo) · Core")
                                color: root.theme.text
                                font.weight: Font.DemiBold
                            }
                            Label {
                                text: qsTr("Private working set: %1 MB").arg(memoryColumn.megabytes(memoryColumn.counters.privateWorkingSet))
                                color: root.theme.text
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            Label {
                                text: qsTr("Working set: %1 MB").arg(memoryColumn.megabytes(memoryColumn.counters.workingSet)) + "\n" + qsTr("Private commit: %1 MB").arg(memoryColumn.megabytes(memoryColumn.counters.privateCommit))
                                color: root.theme.muted
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
                Label {
                    text: qsTr("Client includes the helper service when running; core memory is counted separately.") + "\n" + (root.ui.telemetry.memorySample || "") + "\n" + (root.ui.runtime.platform || "") + " · " + (root.ui.runtime.architecture || "") + " · Clash Meta (mihomo) " + (root.ui.runtime.coreVersion || "—")
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
                    text: qsTr("Core uptime: %1 s").arg(root.ui.telemetry.coreUptimeSeconds || 0) + (root.ui.telemetry.memoryComplete === false ? " · " + qsTr("Resource sample is incomplete") : "")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
