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
        spacing: 14
        GridLayout {
            columns: root.availableWidth >= 600 ? 2 : 1
            columnSpacing: 14
            rowSpacing: 14
            Layout.fillWidth: true
            FlatTile {
                theme: root.theme
                Layout.fillWidth: true
                // Fill the grid row so both overview cards share one height.
                Layout.fillHeight: true
                implicitHeight: statusContent.implicitHeight + 40
                ColumnLayout {
                    id: statusContent
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10
                    Label {
                        font.family: root.theme.fontFamily
                        text: qsTr("Subscription overview")
                        color: root.theme.muted
                        font.bold: true
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: root.ui.runtime.selectedName || qsTr("Start with a subscription")
                        textFormat: Text.PlainText
                        color: root.theme.text
                        font.pixelSize: 23
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: qsTr("%1 profiles · %2 proxy groups · %3 nodes").arg(root.ui.profileModel.count).arg(root.ui.groups.length).arg(root.ui.nodes.length)
                        color: root.theme.muted
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    SubscriptionDetails {
                        visible: !!root.ui.runtime.selectedId
                        Layout.fillWidth: true
                        theme: root.theme
                        profile: root.selectedProfile
                    }
                }
            }
            FlatTile {
                theme: root.theme
                Layout.fillWidth: true
                // Match the subscription overview card height in the same row.
                Layout.fillHeight: true
                implicitHeight: trafficContent.implicitHeight + 40
                ColumnLayout {
                    id: trafficContent
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            font.family: root.theme.fontFamily
                            text: qsTr("Network traffic")
                            color: root.theme.text
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Label {
                            font.family: root.theme.fontFamily
                            text: root.ui.telemetry.traffic ? qsTr("Live core traffic · 1 Hz") : qsTr("Waiting for core data")
                            color: root.theme.muted
                            elide: Text.ElideRight
                        }
                    }
                    TrafficSparkline {
                        // One shared coordinate system: upload (orange) and download (blue).
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        uploadSamples: root.ui.uploadHistory
                        downloadSamples: root.ui.downloadHistory
                        uploadColor: root.theme.upload
                        downloadColor: root.theme.download
                        uploadFillOpacity: 0.25
                        downloadFillOpacity: 0.25
                    }
                    // Color tags stack vertically on the left while both rate
                    // values sit side by side on the right; the card's minimum
                    // width shrinks to one tag column plus two values.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 18
                        ColumnLayout {
                            spacing: 4
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
                                delegate: RowLayout {
                                    id: rateRow
                                    required property var modelData
                                    spacing: 8
                                    Rectangle {
                                        width: 10
                                        height: 10
                                        radius: 3
                                        color: rateRow.modelData.up ? root.theme.upload : root.theme.download
                                    }
                                    Label {
                                        font.family: root.theme.fontFamily
                                        text: rateRow.modelData.label
                                        color: root.theme.muted
                                    }
                                }
                            }
                        }
                        RowLayout {
                            spacing: 18
                            Label {
                                font.family: root.theme.fontFamily
                                text: root.ui.upload
                                color: root.theme.upload
                                font.pixelSize: 20
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Label {
                                font.family: root.theme.fontFamily
                                text: root.ui.download
                                color: root.theme.download
                                font.pixelSize: 20
                                font.bold: true
                                elide: Text.ElideRight
                            }
                        }
                        Item {
                            Layout.fillWidth: true
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
                    enabled: !root.ui.runtime.busy && !!root.ui.runtime.selectedId
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
                        text: qsTr("Network tools")
                        font.bold: true
                        color: root.theme.text
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
                    text: root.ui.runtimeStatus
                    color: root.theme.success
                    font.bold: true
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: root.ui.runtime.ready ? qsTr("Official Clash Meta (mihomo) %1 · Local proxy ready").arg(root.ui.runtime.coreVersion) : qsTr("The network engine runs automatically after a valid profile is selected.")
                    color: root.theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: root.ui.runtimeError
                    visible: text.length > 0
                    color: root.theme.warning
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: width >= 600 ? 2 : 1
                    columnSpacing: 24
                    rowSpacing: 12
                    Repeater {
                        model: ["client", "core"]
                        delegate: ColumnLayout {
                            id: memoryColumn
                            required property string modelData
                            readonly property var counters: (root.ui.telemetry.memory || {})[modelData] || ({})
                            function megabytes(value): string {
                                return value !== undefined ? (Number(value) / 1000000).toFixed(1) : "—";
                            }
                            Layout.fillWidth: true
                            Label {
                                text: memoryColumn.modelData === "client" ? qsTr("QuickClash · Client") : qsTr("Clash Meta (mihomo) · Core")
                                color: root.theme.text
                                font.bold: true
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
