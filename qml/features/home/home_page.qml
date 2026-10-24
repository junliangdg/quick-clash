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
    clip: true
    contentWidth: availableWidth
    function syncDemand(): void {
        ui.setDemand("home:traffic", "traffic", active);
    }
    onActiveChanged: syncDemand()
    Component.onCompleted: syncDemand()
    Component.onDestruction: ui.setDemand("home:traffic", "traffic", false)
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
                        text: root.ui.runtimeError || (root.ui.runtime.ready ? qsTr("Official Mihomo %1 · Local proxy ready").arg(root.ui.runtime.coreVersion) : qsTr("Import and select a profile, then start Mihomo."))
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
                            text: root.ui.runtime.ready || root.ui.runtime.busy ? qsTr("Stop / Cancel") : qsTr("Start core")
                            enabled: root.ui.settingsReady
                            onClicked: root.ui.runtimeCommand(root.ui.runtime.ready || root.ui.runtime.busy ? "stop" : "start")
                        }
                        ActionButton {
                            theme: root.theme
                            text: root.ui.replay ? qsTr("End replay") : qsTr("Preview replay")
                            onClicked: root.ui.replay = !root.ui.replay
                        }
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
                            text: root.ui.replay ? qsTr("Synthetic data · 1 Hz") : qsTr("Waiting for core data")
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
                    checked: !!root.ui.runtime.systemProxy
                    onClicked: root.ui.runtimeCommand("system_proxy", [checked ? "true" : "false"])
                    palette.windowText: root.theme.muted
                }
                Switch {
                    font.family: root.theme.fontFamily
                    text: qsTr("TUN · P3")
                    enabled: false
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
                    text: qsTr("The dashboard does not load the full connection list. Client / core memory: not sampled.")
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
            }
        }
    }
}
