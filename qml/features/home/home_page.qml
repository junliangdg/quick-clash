pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ThemedScrollView {
    form: rootForm
    id: root
    attachScrollBarToContent: true
    required property UiState ui
    property bool active: ui.uiVisible && visible
    property double now: Date.now()
    readonly property bool trafficStale: !ui.replay && !ui.trafficStopped && !!ui.telemetry.traffic && now - Number(ui.telemetry.traffic.time) > 3500
    Timer {
        interval: 1000
        running: root.active
        repeat: true
        onTriggered: root.now = Date.now()
    }
    readonly property string selectedId: ui.runtime.selectedId || ""
    property double profileRevision: 0
    property var selectedProfile: ({
        "id": "",
        "name": ""
    })
    function refreshSelectedProfile(): void {
        selectedProfile = ui.profileModel.profile(selectedId);
    }
    onSelectedIdChanged: refreshSelectedProfile()
    Connections {
        target: root.ui.profileModel
        function onPublicationApplied(): void {
            root.profileRevision += 1;
            root.refreshSelectedProfile();
        }
        function onProfileChanged(profileId: string): void {
            if (profileId === root.selectedId)
                root.refreshSelectedProfile();
        }
    }
    function sizeText(value): string {
        if (value === undefined || value === null || !isFinite(Number(value)))
            return "—";
        return Number(value) >= 1073741824 ? (Number(value) / 1073741824).toFixed(2) + " GiB" : (Number(value) / 1048576).toFixed(1) + " MiB";
    }
    function syncDemand(): void {
        ui.setDemand("home:traffic", "traffic", active);
        ui.setDemand("home:proxies", "proxies", active);
        ui.setDemand("home:resources", "resources", active);
        if (!active) {
            ui.runtimeCommand("cancel_test");
            ui.runtimeCommand("cancel_ip");
        }
    }
    onActiveChanged: {
        if (active)
            now = Date.now();
        syncDemand();
    }
    Component.onCompleted: {
        refreshSelectedProfile();
        syncDemand();
    }
    Component.onDestruction: {
        ui.setDemand("home:traffic", "traffic", false);
        ui.setDemand("home:proxies", "proxies", false);
        ui.runtimeCommand("cancel_test");
        ui.runtimeCommand("cancel_ip");
        ui.setDemand("home:resources", "resources", false);
    }
    contentItem: Flickable {
        id: viewport
        interactive: true
        // The mouse handler below shares dragging with child controls; touch remains Flickable's.
        acceptedButtons: Qt.NoButton
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: root.contentWidth
        contentHeight: root.contentHeight
        DragHandler {
            target: null
            acceptedDevices: PointerDevice.Mouse
            xAxis.enabled: false
            property real previousTranslation: 0
            onActiveChanged: {
                previousTranslation = activeTranslation.y;
                if (active)
                    viewport.cancelFlick();
            }
            onActiveTranslationChanged: {
                if (!active)
                    return;
                const delta = activeTranslation.y - previousTranslation;
                viewport.contentY = Math.max(viewport.originY, Math.min(viewport.originY + Math.max(0, viewport.contentHeight - viewport.height), viewport.contentY - delta));
                previousTranslation = activeTranslation.y;
            }
        }
        ColumnLayout {
            id: rootForm
            spacing: root.theme.gap
            GridLayout {
                columns: root.availableWidth >= 600 ? 2 : 1
                uniformCellWidths: true
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
                        DashboardHeading {
                            theme: root.theme
                            source: "qrc:/icons/navigation/subscriptions.svg"
                            text: qsTr("Current subscription")
                            Layout.fillWidth: true
                        }
                        ThemedComboBox {
                            id: profileSelector
                            theme: root.theme
                            objectName: "home_profile_selector"
                            Accessible.name: qsTr("Current subscription")
                            Layout.fillWidth: true
                            model: root.ui.profileModel
                            textRole: "name"
                            valueRole: "profileId"
                            currentIndex: {
                                // indexOfValue does not track model updates or initial role setup.
                                if (root.profileRevision === 0 || profileSelector.count === 0 || profileSelector.valueRole !== "profileId")
                                    return -1;
                                return indexOfValue(root.selectedId);
                            }
                            displayText: root.selectedId ? (root.ui.runtime.selectedName || root.selectedProfile.name || qsTr("Start with a subscription")) : qsTr("Start with a subscription")
                            font.weight: Font.DemiBold
                            enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly && root.ui.profileModel.count > 0
                            onActivated: root.ui.runtimeCommand("select", [currentValue])
                            Component.onCompleted: root.profileRevision += 1
                        }
                        Label {
                            objectName: "home_proxy_counts"
                            text: qsTr("%1 proxy groups · %2 nodes").arg(root.ui.groupCount).arg(root.ui.nodeCount)
                            color: root.theme.muted
                            font.pixelSize: root.theme.scaledSize(12)
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        SubscriptionDetails {
                            objectName: "home_subscription_details"
                            visible: !!root.selectedId
                            Layout.fillWidth: true
                            theme: root.theme
                            profile: root.selectedProfile
                            overview: true
                        }
                        Item {
                            Layout.fillHeight: true
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            ActionButton {
                                width: Math.min(implicitWidth, parent.width)
                                theme: root.theme
                                text: qsTr("Update subscription")
                                objectName: "home_update_subscription"
                                icon.source: "qrc:/icons/dashboard/refresh_cw.svg"
                                visible: !!root.selectedProfile.secretRef
                                enabled: !root.ui.runtime.busy && !root.ui.runtime.readOnly
                                onClicked: root.ui.runtimeCommand("update", [root.selectedId])
                            }
                            ActionButton {
                                width: Math.min(implicitWidth, parent.width)
                                theme: root.theme
                                text: qsTr("Manage subscriptions")
                                ghost: true
                                visible: !!root.selectedId
                                onClicked: root.ui.navigate("subscriptions")
                            }
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
                        DashboardHeading {
                            theme: root.theme
                            source: "qrc:/icons/navigation/tests.svg"
                            text: qsTr("Network traffic")
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            ColumnLayout {
                                spacing: 4
                                RowLayout {
                                    spacing: 6
                                    UiIcon {
                                        theme: root.theme
                                        source: "qrc:/icons/dashboard/arrow_down.svg"
                                        implicitWidth: root.theme.scaledSize(14)
                                        implicitHeight: root.theme.scaledSize(14)
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
                                    UiIcon {
                                        theme: root.theme
                                        source: "qrc:/icons/dashboard/arrow_up.svg"
                                        implicitWidth: root.theme.scaledSize(14)
                                        implicitHeight: root.theme.scaledSize(14)
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
                                model: root.trafficStale ? ["—", "—"] : [root.ui.download, root.ui.upload]
                                delegate: RowLayout {
                                    id: rateMetric
                                    required property string modelData
                                    required property int index
                                    readonly property int unitOffset: modelData.lastIndexOf(" ")
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 4
                                    Item {
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: rateMetric.unitOffset < 0 ? rateMetric.modelData : rateMetric.modelData.slice(0, rateMetric.unitOffset)
                                        color: root.trafficStale ? root.theme.muted : rateMetric.index === 0 ? root.theme.download : root.theme.upload
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
                        Label {
                            objectName: "traffic_scale"
                            visible: root.ui.trafficHasHistory || !!root.ui.telemetry.traffic || root.ui.replay
                            Layout.fillWidth: true
                            text: chart.peak >= 1048576 ? (chart.peak / 1048576).toFixed(2) + " GiB/s" : chart.peak >= 1024 ? (chart.peak / 1024).toFixed(2) + " MiB/s" : chart.peak.toFixed(1) + " KiB/s"
                            color: root.theme.muted
                            font.pixelSize: root.theme.supportingSize
                        }
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 76
                            TrafficSparkline {
                                id: chart
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
                                objectName: "traffic_empty"
                                anchors.centerIn: parent
                                visible: !root.ui.replay && !root.ui.trafficStopped && !root.ui.trafficHasHistory && (!root.ui.telemetry.traffic || root.trafficStale)
                                text: root.trafficStale ? qsTr("Traffic data is stale") : qsTr("Waiting for core data")
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
                        Label {
                            objectName: "home_traffic_totals"
                            text: qsTr("Current core session totals") + "\n↓ " + root.sizeText((root.ui.telemetry.traffic || {}).downTotal) + " · ↑ " + root.sizeText((root.ui.telemetry.traffic || {}).upTotal)
                            color: root.theme.muted
                            font.pixelSize: root.theme.supportingSize
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
            FlatTile {
                objectName: "home_proxy_card"
                theme: root.theme
                Layout.fillWidth: true
                implicitHeight: proxyContent.implicitHeight + 2 * root.theme.panelPadding
                HomeProxyControls {
                    id: proxyContent
                    anchors.fill: parent
                    anchors.margins: root.theme.panelPadding
                    ui: root.ui
                    theme: root.theme
                }
            }
            FlatTile {
                objectName: "home_tools_card"
                theme: root.theme
                Layout.fillWidth: true
                implicitHeight: tools.implicitHeight + 2 * root.theme.panelPadding
                NetworkTools {
                    id: tools
                    anchors.fill: parent
                    anchors.margins: root.theme.panelPadding
                    ui: root.ui
                    theme: root.theme
                }
            }
            FlatTile {
                theme: root.theme
                Layout.fillWidth: true
                objectName: "home_system_card"
                implicitHeight: overview.implicitHeight + 2 * root.theme.panelPadding
                RuntimeOverview {
                    id: overview
                    anchors.fill: parent
                    anchors.margins: root.theme.panelPadding
                    ui: root.ui
                    theme: root.theme
                    now: root.now
                }
            }
        }
    }
}
