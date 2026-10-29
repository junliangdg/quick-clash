pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ScrollView {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    contentWidth: availableWidth
    clip: true
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
        return outcomeText(result) + " · HTTP " + (result.status || "—")
            + " · " + (result.elapsedMs === undefined ? "—" : result.elapsedMs) + " ms · "
            + (result.resolved || result.path || "") + " · " + (result.mode || "")
            + "\n" + (result.finalOrigin || "") + " · " + (result.time || "")
            + "\n" + Object.keys(result.selections || {}).map(function (name) {
                return name + ": " + result.selections[name];
            }).join(" · ");
    }
    Connections {
        target: root.ui
        function onUiVisibleChanged(): void { if (!root.ui.uiVisible) root.ui.runtimeCommand("cancel_test"); }
    }
    onVisibleChanged: { if (!visible) ui.runtimeCommand("cancel_test"); }
    Component.onDestruction: ui.runtimeCommand("cancel_test")
    FlatTile {
        width: parent.width
        theme: root.theme
        implicitHeight: content.implicitHeight + 32
        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: root.theme.panelPadding
            spacing: root.theme.gap
            Label {
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
                text: qsTr("Enter up to 8 HTTPS URLs, one per line. Each request follows up to 3 HTTPS redirects within one 64 KiB and 12 second limit. Requests run one at a time without cookies. Direct requests still follow TUN and system routes. A reachable entry does not prove node forwarding.")
                color: root.theme.muted
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Run test")
                prominent: true
                enabled: (route.currentIndex !== 0 || !!root.ui.runtime.ready) && ((root.ui.runtime.features || {}).websiteTest || {}).state !== "Running"
                onClicked: {
                    const urls = target.text.split(/\r?\n/).map(text => text.trim()).filter(text => text.length > 0);
                    if (urls.length > 0 && urls.length <= 8) root.ui.runtimeCommand("test_batch", [route.model[route.currentIndex].key].concat(urls));
                }
            }
            Label {
                text: root.ui.runtime.testResult || ""
                color: root.theme.text
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            ActionButton {
                theme: root.theme
                text: qsTr("Cancel test")
                visible: ((root.ui.runtime.features || {}).websiteTest || {}).state === "Running"
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
            }
            Label {
                text: qsTr("Recent results: %1 / 32").arg(((root.ui.runtime.features || {}).testHistory || []).length)
                color: root.theme.muted
            }
            Repeater {
                model: ((root.ui.runtime.features || {}).testHistory || []).slice().reverse()
                delegate: Label {
                    required property var modelData
                    text: (modelData.target || "") + "\n" + root.resultDetails(modelData)
                    color: root.theme.text
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
