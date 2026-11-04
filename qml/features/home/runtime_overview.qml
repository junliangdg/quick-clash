pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ColumnLayout {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    required property double now
    readonly property bool expanded: ui.dashboard.resources === true
    readonly property var memory: ui.telemetry.memory || ({})
    readonly property var effective: (ui.runtime.settingsState || {}).effective || ({})
    readonly property double sampleTime: Date.parse(ui.telemetry.memorySample || "")
    readonly property bool stale: isFinite(sampleTime) && now - sampleTime > 15000
    readonly property int proxyPort: Number(effective.mixedPort || 0)
    readonly property string proxyAddress: ui.runtime.ready === true && (ui.runtime.settingsState || {}).running === true && proxyPort > 0 && proxyPort <= 65535 ? "127.0.0.1:" + proxyPort : ""
    property string copiedAddress: ""
    onProxyAddressChanged: copiedAddress = ""
    function memoryText(process, field): string {
        const value = (memory[process] || {})[field];
        if ((process === "core" && !ui.runtime.ready) || !isFinite(sampleTime) || stale || value === undefined || value === null || value === "" || !isFinite(Number(value)) || Number(value) < 0)
            return "—";
        return (Number(value) / 1000000).toFixed(1) + " MB";
    }
    function uptimeText(): string {
        if (!ui.runtime.ready)
            return qsTr("Not running");
        const value = ui.telemetry.coreUptimeSeconds;
        if (value === undefined || value === null || !isFinite(Number(value)) || Number(value) < 0)
            return "—";
        const minutes = Math.floor(Number(value) / 60);
        return minutes < 1 ? qsTr("Less than a minute") : minutes < 60 ? qsTr("%1 min").arg(minutes) : qsTr("%1 h %2 min").arg(Math.floor(minutes / 60)).arg(minutes % 60);
    }
    spacing: 12
    DashboardHeading {
        theme: root.theme
        source: "qrc:/icons/dashboard/cpu.svg"
        Layout.fillWidth: true
        text: qsTr("Runtime overview")
    }
    GridLayout {
        objectName: "home_runtime_metrics"
        Layout.fillWidth: true
        columns: width >= 800 ? 4 : width >= 480 ? 2 : 1
        uniformCellWidths: true
        columnSpacing: 24
        rowSpacing: 10
        Repeater {
            model: [
                {
                    key: "identity",
                    icon: "qrc:/icons/dashboard/cpu.svg",
                    label: qsTr("Core"),
                    value: (root.ui.runtime.coreName || "—") + " " + (root.ui.runtime.coreVersion || "—")
                },
                {
                    key: "client",
                    icon: "qrc:/icons/dashboard/memory_stick.svg",
                    label: qsTr("Client memory"),
                    value: root.memoryText("client", "privateWorkingSet")
                },
                {
                    key: "core",
                    icon: "qrc:/icons/dashboard/memory_stick.svg",
                    label: qsTr("Core memory"),
                    value: root.memoryText("core", "privateWorkingSet")
                },
                {
                    key: "uptime",
                    icon: "qrc:/icons/dashboard/clock.svg",
                    label: qsTr("Current core session"),
                    value: root.uptimeText()
                }
            ]
            delegate: ColumnLayout {
                id: metric
                required property var modelData
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 180
                Layout.alignment: Qt.AlignTop
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    UiIcon {
                        theme: root.theme
                        source: metric.modelData.icon
                        implicitWidth: root.theme.scaledSize(14)
                        implicitHeight: root.theme.scaledSize(14)
                    }
                    Label {
                        text: metric.modelData.label
                        color: root.theme.muted
                        font.pixelSize: root.theme.supportingSize
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
                Label {
                    objectName: "home_runtime_" + metric.modelData.key
                    text: metric.modelData.value
                    textFormat: Text.PlainText
                    color: metric.modelData.key === "identity" ? root.theme.info : root.theme.text
                    font.pixelSize: root.theme.scaledSize(21)
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }
    Label {
        objectName: "home_resource_status"
        text: [qsTr("Private working set"), root.stale ? qsTr("Resource data is stale") : root.ui.telemetry.memoryComplete === false ? qsTr("Partial statistics") : "", isFinite(root.sampleTime) ? qsTr("Updated %1").arg(new Date(root.sampleTime).toLocaleTimeString(Qt.locale())) : qsTr("Waiting for resource data")].filter(value => !!value).join(" · ")
        color: root.stale || root.ui.telemetry.memoryComplete === false ? root.theme.warning : root.theme.muted
        font.pixelSize: root.theme.supportingSize
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        HoverHandler {
            id: memoryHover
        }
        ThemedToolTip {
            theme: root.theme
            visible: memoryHover.hovered
            text: qsTr("Private working set is resident memory that is not shared. Client memory includes the helper service when running; core memory is separate.")
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        Label {
            objectName: "home_proxy_address"
            text: qsTr("Local proxy") + " · " + (root.proxyAddress || qsTr("Unavailable"))
            color: root.theme.muted
            width: Math.min(implicitWidth, root.width)
            height: Math.max(implicitHeight, copyAddress.height)
            wrapMode: Text.WrapAnywhere
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: root.theme.supportingSize
        }
        ActionButton {
            id: copyAddress
            width: Math.min(implicitWidth, parent.width)
            objectName: "home_copy_proxy_address"
            theme: root.theme
            text: root.proxyAddress && root.copiedAddress === root.proxyAddress ? qsTr("Copied") : qsTr("Copy address")
            icon.source: "qrc:/icons/dashboard/copy.svg"
            ghost: true
            enabled: !!root.proxyAddress
            onClicked: {
                root.ui.copyText(root.proxyAddress);
                root.copiedAddress = root.proxyAddress;
            }
            ThemedToolTip {
                theme: root.theme
                visible: copyAddress.hovered
                text: qsTr("HTTP and SOCKS5 share this local proxy address.")
            }
        }
        ActionButton {
            width: Math.min(implicitWidth, parent.width)
            theme: root.theme
            objectName: "home_resource_toggle"
            text: qsTr("Details")
            icon.source: root.expanded ? "qrc:/icons/dashboard/chevron_down.svg" : "qrc:/icons/dashboard/chevron_right.svg"
            ghost: true
            enabled: root.ui.settingsReady
            onClicked: root.ui.setDashboardOption("resources", !root.expanded)
        }
    }
    Loader {
        objectName: "home_resource_details_loader"
        Layout.fillWidth: true
        active: root.expanded
        visible: active
        sourceComponent: ColumnLayout {
            objectName: "home_resource_details"
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: root.theme.border
            }
            GridLayout {
                Layout.fillWidth: true
                columns: width >= 600 ? 2 : 1
                columnSpacing: 24
                rowSpacing: 12
                Repeater {
                    model: [
                        {
                            key: "client",
                            label: qsTr("Client total")
                        },
                        {
                            key: "core",
                            label: qsTr("Core memory")
                        },
                        {
                            key: "service",
                            label: qsTr("Helper service")
                        },
                        {
                            key: "validator",
                            label: qsTr("Configuration validator")
                        },
                        {
                            key: "serviceValidator",
                            label: qsTr("Service configuration validator")
                        }
                    ]
                    delegate: ColumnLayout {
                        id: processDetails
                        required property var modelData
                        visible: modelData.key === "client" || modelData.key === "core" || Object.keys(root.memory[modelData.key] || {}).length > 0
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.alignment: Qt.AlignTop
                        spacing: 4
                        Label {
                            text: processDetails.modelData.label
                            color: root.theme.text
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            text: qsTr("Private working set: %1").arg(root.memoryText(processDetails.modelData.key, "privateWorkingSet")) + "\n" + qsTr("Working set: %1").arg(root.memoryText(processDetails.modelData.key, "workingSet")) + "\n" + qsTr("Private commit: %1").arg(root.memoryText(processDetails.modelData.key, "privateCommit"))
                            color: root.theme.muted
                            font.pixelSize: root.theme.supportingSize
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
            Label {
                text: qsTr("Client total includes the application and helper service when running. Core and temporary validators are counted separately.") + "\n" + qsTr("Execution: %1").arg(root.ui.runtime.coreExecutionMode === "Service" ? qsTr("Helper service") : root.ui.runtime.coreExecutionMode === "Local" ? qsTr("Local process") : qsTr("Not running")) + " · " + (root.ui.runtime.platform || "—") + " · " + (root.ui.runtime.architecture || "—")
                color: root.theme.muted
                textFormat: Text.PlainText
                font.pixelSize: root.theme.supportingSize
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
    }
}
