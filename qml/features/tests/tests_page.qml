pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ThemedScrollView {
    id: root
    form: testBody
    required property UiState ui
    readonly property var urls: target.text.split(/\r?\n/).map(text => text.trim()).filter(text => text.length > 0)
    readonly property bool running: ((ui.runtime.features || {}).websiteTest || {}).state === "Running"
    function outcomeText(result): string {
        const states = {Running: qsTr("Testing"), Reachable: qsTr("Entry reachable"),
            HttpError: qsTr("HTTP error"), DnsError: qsTr("DNS error"),
            TlsError: qsTr("TLS error"), ProxyUnavailable: qsTr("Proxy unavailable"),
            Timeout: qsTr("Timed out"), ResponseLimit: qsTr("Response exceeds 64 KiB"),
            NetworkError: qsTr("Network error"), Cancelled: qsTr("Cancelled"),
            Stale: qsTr("Stale"), RedirectBlocked: qsTr("Unsafe redirect blocked"),
            RedirectLimit: qsTr("Redirect limit or loop")};
        return (result.stale ? qsTr("Stale") + " · " : "") + (states[result.state] || result.state || "");
    }
    function resultDetails(result): string {
        const selections = Object.keys(result.selections || {}).map(function (name) {
                return name + ": " + result.selections[name];
            }).join(" · ");
        return [outcomeText(result), result.status ? "HTTP " + result.status : "",
            result.elapsedMs === undefined ? "" : result.elapsedMs + " ms",
            pathText(result.resolved || result.path), result.mode || "",
            result.finalOrigin || "", timeText(result.time), selections].filter(value => !!value).join(" · ");
    }
    function resultSummary(result): string {
        return [outcomeText(result), result.status ? "HTTP " + result.status : "",
            result.elapsedMs === undefined ? "" : result.elapsedMs + " ms",
            pathText(result.resolved || result.path), timeText(result.time)].filter(value => !!value).join(" · ");
    }
    function extraDetails(result): string {
        const selections = Object.keys(result.selections || {}).map(function (name) {
            return name + ": " + result.selections[name];
        }).join(" · ");
        return [result.mode || "", result.finalOrigin || "", selections].filter(value => !!value).join(" · ");
    }
    function pathText(path): string {
        const labels = {CurrentProxy: qsTr("Current proxy"), Direct: qsTr("Direct"), SystemDefault: qsTr("System proxy settings")};
        return labels[path] || path || "";
    }
    function timeText(value): string {
        if (!value)
            return "";
        const date = new Date(value);
        return isNaN(date.getTime()) ? value : date.toLocaleString(Qt.locale(), Locale.ShortFormat);
    }
    Connections {
        target: root.ui
        function onUiVisibleChanged(): void { if (!root.ui.uiVisible) root.ui.runtimeCommand("cancel_test"); }
    }
    onVisibleChanged: { if (!visible) ui.runtimeCommand("cancel_test"); }
    Component.onDestruction: ui.runtimeCommand("cancel_test")
    FlatTile {
        id: testBody
        theme: root.theme
        implicitHeight: content.implicitHeight + 32
        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: root.theme.panelPadding
            spacing: root.theme.gap
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrapMode: Text.Wrap
                text: qsTr("Website test")
                color: root.theme.text
                font.pixelSize: root.theme.bodySize
                font.weight: Font.DemiBold
            }
            ThemedComboBox {
                theme: root.theme
                id: route
                objectName: "website_route"
                Accessible.name: qsTr("Request path")
                Layout.fillWidth: true
                model: [{key: "CurrentProxy", label: qsTr("Current proxy")}, {key: "Direct", label: qsTr("Direct (system routes still apply)")}, {key: "SystemDefault", label: qsTr("System proxy settings")}]
                textRole: "label"
            }
            ThemedTextArea {
                theme: root.theme
                id: target
                objectName: "website_targets"
                Accessible.name: qsTr("HTTPS website targets")
                Layout.fillWidth: true
                placeholderText: qsTr("https://your-website.example")
                wrapMode: TextEdit.Wrap
                Layout.preferredHeight: 110
                text: root.ui.runtime.testTarget || ""
            }
            Label {
                objectName: "website_target_count"
                text: root.urls.length === 0 ? qsTr("Enter 1–8 HTTPS URLs, one per line.") : root.urls.length > 8 ? qsTr("Too many addresses: %1 / 8").arg(root.urls.length) : qsTr("Addresses: %1 / 8").arg(root.urls.length)
                color: root.urls.length > 8 ? root.theme.error : root.theme.muted
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Label {
                text: qsTr("Requests run one at a time without cookies (up to 3 HTTPS redirects, 64 KiB and 12 seconds each). Direct requests still follow TUN and system routes. A reachable entry does not prove node forwarding.")
                color: root.theme.muted
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Run test")
                objectName: "run_website_test"
                prominent: true
                enabled: root.urls.length > 0 && root.urls.length <= 8 && !root.ui.runtime.busy && (route.currentIndex !== 0 || !!root.ui.runtime.ready) && !root.running
                onClicked: root.ui.runtimeCommand("test_batch", [route.model[route.currentIndex].key].concat(root.urls))
            }
            Label {
                text: root.ui.runtime.testResult || ""
                visible: text.length > 0
                color: root.theme.text
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Cancel test")
                visible: root.running
                onClicked: root.ui.runtimeCommand("cancel_test")
            }
            Label {
                text: {
                    const result = (root.ui.runtime.features || {}).websiteTest || {};
                    return result.state ? root.resultDetails(result) : "";
                }
                color: root.theme.muted
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: text.length > 0
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrapMode: Text.Wrap
                text: qsTr("Recent results: %1 / 32").arg(((root.ui.runtime.features || {}).testHistory || []).length)
                color: root.theme.muted
            }
            Repeater {
                model: ((root.ui.runtime.features || {}).testHistory || []).slice().reverse()
                delegate: FlatTile {
                    id: resultCard
                    required property var modelData
                    objectName: "website_result_card"
                    property bool detailsOpen: false
                    theme: root.theme
                    Layout.fillWidth: true
                    implicitHeight: resultContent.implicitHeight + 24
                    ColumnLayout {
                        id: resultContent
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4
                        Label {
                            text: resultCard.modelData.target || ""
                            color: root.theme.text
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            textFormat: Text.PlainText
                        }
                        Label {
                            objectName: "website_result_summary"
                            text: root.resultSummary(resultCard.modelData)
                            color: resultCard.modelData.stale ? root.theme.warning : resultCard.modelData.state === "Reachable" ? root.theme.success : root.theme.muted
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            textFormat: Text.PlainText
                        }
                        ActionButton {
                            objectName: "website_result_details_toggle"
                            theme: root.theme
                            visible: root.extraDetails(resultCard.modelData).length > 0
                            text: resultCard.detailsOpen ? qsTr("Hide details") : qsTr("Show details")
                            ghost: true
                            onClicked: resultCard.detailsOpen = !resultCard.detailsOpen
                        }
                        Label {
                            objectName: "website_result_details"
                            visible: resultCard.detailsOpen
                            text: root.extraDetails(resultCard.modelData)
                            color: root.theme.muted
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            textFormat: Text.PlainText
                        }
                    }
                }
            }
            Label {
                visible: ((root.ui.runtime.features || {}).testHistory || []).length === 0
                text: qsTr("No test results yet")
                color: root.theme.muted
                Layout.fillWidth: true
            }
        }
    }
}
