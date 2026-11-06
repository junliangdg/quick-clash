pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ColumnLayout {
    id: root
    objectName: "home_network_tools"
    required property UiState ui
    required property ThemeTokens theme
    property string family: "v4"
    readonly property bool hiddenAddresses: ui.dashboard.hideAddresses === true
    readonly property bool details: ui.dashboard.details === true
    readonly property var features: ui.runtime.features || ({})
    readonly property var info: features.ipInfo || ({})
    readonly property var pair: info[family] || ({})
    readonly property bool querying: (pair.local || {}).state === "Running" || (pair.proxy || {}).state === "Running"
    readonly property bool testing: (features.websiteTest || {}).state === "Running"
    readonly property var sites: [
        {
            name: "Google",
            icon: "qrc:/icons/sites/google.svg",
            url: "https://www.google.com/generate_204"
        },
        {
            name: "YouTube",
            icon: "qrc:/icons/sites/youtube.svg",
            url: "https://www.youtube.com/generate_204"
        },
        {
            name: "GitHub",
            icon: "qrc:/icons/sites/github.svg",
            url: "https://api.github.com/zen"
        }
    ]
    function stateText(state): string {
        const labels = {
            Ready: qsTr("Fetched"),
            Reachable: qsTr("Reachable"),
            Running: qsTr("Testing"),
            Timeout: qsTr("Timed out"),
            TlsError: qsTr("TLS error"),
            DnsError: qsTr("DNS error"),
            HttpError: qsTr("HTTP error"),
            ProxyUnavailable: qsTr("Proxy unavailable"),
            Cancelled: qsTr("Cancelled"),
            Stale: qsTr("Stale"),
            ResponseLimit: qsTr("Response too large"),
            InvalidResponse: qsTr("Invalid response"),
            RedirectBlocked: qsTr("Redirect blocked"),
            RedirectLimit: qsTr("Too many redirects"),
            NetworkError: qsTr("Network error")
        };
        return labels[state] || qsTr("Not queried");
    }
    function siteResult(url): var {
        const current = features.websiteTest || {};
        if (current.target === url && current.path === "CurrentProxy" && current.state === "Running")
            return current;
        const history = features.testHistory || [];
        for (let i = history.length - 1; i >= 0; --i)
            if (history[i].target === url && history[i].path === "CurrentProxy")
                return history[i];
        return ({});
    }
    function resultColor(result): color {
        if (result.stale || result.state === "Stale")
            return theme.warning;
        if (result.state === "Reachable" || result.state === "Ready")
            return theme.success;
        if (result.state === "Running")
            return theme.info;
        return ["Timeout", "TlsError", "DnsError", "HttpError", "ProxyUnavailable", "ResponseLimit", "InvalidResponse", "RedirectBlocked", "RedirectLimit", "NetworkError"].includes(result.state) ? theme.error : theme.muted;
    }
    spacing: 12
    DashboardHeading {
        theme: root.theme
        source: "qrc:/icons/dashboard/wrench.svg"
        text: qsTr("Network tools")
        Layout.fillWidth: true
    }
    Flow {
        Layout.fillWidth: true
        spacing: 12
        ThemedSwitch {
            width: Math.min(implicitWidth, parent.width)
            theme: root.theme
            objectName: "home_ip_toggle"
            text: qsTr("Show IP comparison")
            checked: root.ui.dashboard.ip === true
            enabled: root.ui.settingsReady
            onClicked: {
                root.ui.setDashboardOption("ip", checked);
                if (!checked)
                    root.ui.runtimeCommand("cancel_ip");
            }
        }
        ThemedSwitch {
            width: Math.min(implicitWidth, parent.width)
            theme: root.theme
            objectName: "home_sites_toggle"
            text: qsTr("Show common websites")
            checked: root.ui.dashboard.sites === true
            enabled: root.ui.settingsReady
            onClicked: {
                root.ui.setDashboardOption("sites", checked);
                if (!checked)
                    root.ui.runtimeCommand("cancel_test");
            }
        }
    }
    Loader {
        Layout.fillWidth: true
        active: root.ui.dashboard.ip === true
        visible: active
        sourceComponent: ColumnLayout {
            objectName: "home_ip_section"
            Layout.fillWidth: true
            spacing: 12
            Flow {
                Layout.fillWidth: true
                spacing: 8
                RowLayout {
                    spacing: 8
                    height: ipFamily.height
                    UiIcon {
                        theme: root.theme
                        source: "qrc:/icons/dashboard/globe.svg"
                        color: root.theme.info
                    }
                    Label {
                        text: qsTr("IP comparison")
                        color: root.theme.text
                        font.weight: Font.DemiBold
                    }
                }
                ThemedComboBox {
                    id: ipFamily
                    width: Math.min(implicitWidth, parent.width)
                    theme: root.theme
                    objectName: "home_ip_family"
                    Accessible.name: qsTr("IP address family")
                    model: ["IPv4", "IPv6"]
                    currentIndex: root.family === "v6" ? 1 : 0
                    onActivated: {
                        root.ui.runtimeCommand("cancel_ip");
                        root.family = currentIndex === 1 ? "v6" : "v4";
                    }
                }
                ActionButton {
                    width: Math.min(implicitWidth, parent.width)
                    theme: root.theme
                    objectName: "home_query_ip"
                    text: root.querying ? qsTr("Cancel query") : qsTr("Query / refresh IP")
                    icon.source: "qrc:/icons/dashboard/refresh_cw.svg"
                    enabled: !root.ui.runtime.busy
                    onClicked: root.ui.runtimeCommand(root.querying ? "cancel_ip" : "query_ip", [root.family])
                }
                ActionButton {
                    width: Math.min(implicitWidth, parent.width)
                    theme: root.theme
                    text: root.hiddenAddresses ? qsTr("Show addresses") : qsTr("Hide addresses")
                    icon.source: root.hiddenAddresses ? "qrc:/icons/dashboard/eye.svg" : "qrc:/icons/dashboard/eye_off.svg"
                    ghost: true
                    enabled: root.ui.settingsReady
                    onClicked: root.ui.setDashboardOption("hideAddresses", !root.hiddenAddresses)
                }
            }
            Label {
                text: qsTr("Manual queries contact IP.SB using each path. No automatic requests. System routes, TUN and other VPNs still apply.")
                color: root.theme.muted
                font.pixelSize: root.theme.supportingSize
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            GridLayout {
                Layout.fillWidth: true
                columns: width >= 580 ? 2 : 1
                columnSpacing: 24
                rowSpacing: 16
                Repeater {
                    model: ["local", "proxy"]
                    delegate: ColumnLayout {
                        id: ipColumn
                        required property string modelData
                        readonly property var result: root.pair[modelData] || ({})
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 300
                        Layout.alignment: Qt.AlignTop
                        spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            UiIcon {
                                theme: root.theme
                                source: ipColumn.modelData === "proxy" ? "qrc:/icons/navigation/proxies.svg" : "qrc:/icons/dashboard/globe.svg"
                                color: root.theme.info
                            }
                            Label {
                                text: ipColumn.modelData === "proxy" ? qsTr("Current proxy entry exit") : root.ui.runtime.tunState === "On" ? qsTr("Exit without application proxy") : qsTr("Local public IP")
                                color: root.theme.text
                                font.weight: Font.DemiBold
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }
                        Label {
                            text: root.hiddenAddresses ? "••••••" : ipColumn.result.ip || "—"
                            color: root.hiddenAddresses || !ipColumn.result.ip ? root.theme.muted : root.theme.info
                            font.pixelSize: root.theme.scaledSize(19)
                            font.weight: Font.DemiBold
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                        }
                        Label {
                            text: qsTr("ISP: %1").arg(ipColumn.result.isp || qsTr("Not provided")) + (ipColumn.result.organization && ipColumn.result.organization !== ipColumn.result.isp ? "\n" + qsTr("Organization: %1").arg(ipColumn.result.organization) : "") + "\n" + qsTr("Location: %1").arg([ipColumn.result.country, ipColumn.result.region, ipColumn.result.city].filter(v => !!v).join(" · ") || qsTr("Not provided"))
                            color: root.theme.muted
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: root.theme.supportingSize
                        }
                        Label {
                            text: root.stateText(ipColumn.result.state) + (ipColumn.result.stale ? " · " + qsTr("Stale · refresh required") : "")
                            color: root.resultColor(ipColumn.result)
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: root.theme.supportingSize
                        }
                        Label {
                            visible: !!(ipColumn.result.time || ipColumn.result.attemptedAt)
                            text: new Date(ipColumn.result.time || ipColumn.result.attemptedAt).toLocaleString(Qt.locale()) + " · " + (ipColumn.result.source || "")
                            color: root.theme.muted
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: root.theme.supportingSize
                        }
                        ActionButton {
                            theme: root.theme
                            text: qsTr("Copy IP")
                            icon.source: "qrc:/icons/dashboard/copy.svg"
                            ghost: true
                            enabled: !!ipColumn.result.ip && !root.hiddenAddresses
                            onClicked: root.ui.copyText(ipColumn.result.ip)
                        }
                    }
                }
            }
            Label {
                text: qsTr("Location is IP database attribution. Rules may select DIRECT; identical IPs do not prove proxy failure.")
                color: root.theme.muted
                font.pixelSize: root.theme.supportingSize
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            ActionButton {
                theme: root.theme
                text: qsTr("More IP and local network details")
                icon.source: root.details ? "qrc:/icons/dashboard/chevron_down.svg" : "qrc:/icons/dashboard/chevron_right.svg"
                ghost: true
                enabled: root.ui.settingsReady
                onClicked: root.ui.setDashboardOption("details", !root.details)
            }
            Label {
                visible: root.details
                text: root.hiddenAddresses ? "••••••" : "ASN · " + ((root.pair.local || {}).asn || "—") + " / " + ((root.pair.proxy || {}).asn || "—") + "\n" + (root.info.interfaces || []).map(v => v.name + " · " + v.ip).join("\n")
                color: root.theme.muted
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
            }
        }
    }
    Rectangle {
        visible: root.ui.dashboard.ip === true && root.ui.dashboard.sites === true
        Layout.fillWidth: true
        implicitHeight: 1
        color: root.theme.border
    }
    Loader {
        Layout.fillWidth: true
        active: root.ui.dashboard.sites === true
        visible: active
        sourceComponent: ColumnLayout {
            objectName: "home_sites_section"
            Layout.fillWidth: true
            spacing: 12
            Flow {
                Layout.fillWidth: true
                spacing: 8
                RowLayout {
                    spacing: 8
                    height: testAll.height
                    UiIcon {
                        theme: root.theme
                        source: "qrc:/icons/dashboard/globe.svg"
                        color: root.theme.info
                    }
                    Label {
                        text: qsTr("Common websites")
                        color: root.theme.text
                        font.weight: Font.DemiBold
                    }
                }
                ActionButton {
                    id: testAll
                    width: Math.min(implicitWidth, parent.width)
                    theme: root.theme
                    objectName: "home_test_all"
                    text: root.testing ? qsTr("Cancel test") : qsTr("Test all")
                    icon.source: "qrc:/icons/dashboard/gauge.svg"
                    enabled: root.testing || (!!root.ui.runtime.ready && !root.ui.runtime.busy)
                    onClicked: root.ui.runtimeCommand(root.testing ? "cancel_test" : "test_batch", ["CurrentProxy"].concat(root.sites.map(v => v.url)))
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: width >= 680 ? 3 : 1
                columnSpacing: 12
                rowSpacing: 8
                Repeater {
                    model: root.sites
                    delegate: ColumnLayout {
                        id: site
                        required property var modelData
                        readonly property var result: root.siteResult(modelData.url)
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 200
                        Layout.alignment: Qt.AlignTop
                        spacing: 4
                        ActionButton {
                            theme: root.theme
                            text: site.modelData.name
                            icon.source: site.modelData.icon
                            icon.width: root.theme.scaledSize(18)
                            icon.height: root.theme.scaledSize(18)
                            icon.color: !enabled ? root.theme.muted : site.modelData.name === "Google" ? root.theme.info : site.modelData.name === "YouTube" ? root.theme.error : root.theme.text
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            enabled: !!root.ui.runtime.ready && !root.ui.runtime.busy && !root.testing
                            onClicked: root.ui.runtimeCommand("test_batch", ["CurrentProxy", site.modelData.url])
                        }
                        Label {
                            text: (site.result.state ? root.stateText(site.result.state) : qsTr("Not tested")) + (site.result.elapsedMs !== undefined ? " · " + site.result.elapsedMs + " ms" : "") + (site.result.stale ? " · " + qsTr("Stale") : "")
                            color: root.resultColor(site.result)
                            font.weight: Font.DemiBold
                            font.pixelSize: root.theme.supportingSize
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            visible: !!site.result.status || !!site.result.time
                            text: [(site.result.status ? "HTTP " + site.result.status : ""), (site.result.time ? new Date(site.result.time).toLocaleTimeString(Qt.locale()) : "")].filter(value => !!value).join(" · ")
                            color: root.theme.muted
                            font.pixelSize: root.theme.supportingSize
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
            Label {
                text: qsTr("Current proxy entry · follows rules · total HTTPS request time, not bandwidth or streaming availability.")
                color: root.theme.muted
                font.pixelSize: root.theme.supportingSize
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
    }
}
