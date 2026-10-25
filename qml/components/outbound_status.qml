import QtQuick
import QtQuick.Controls
import QuickClash.Ui

Label {
    required property UiState ui
    required property ThemeTokens theme
    property bool compact: false
    readonly property var outbound: ui.runtime.outbound || ({})
    text: {
        const chain = outbound.chain || [];
        const path = compact && outbound.node && chain.length > 1
                   ? outbound.policy + " → " + outbound.node : chain.join(" → ");
        const heading = outbound.mode === "direct" ? qsTr("Direct connection")
                      : outbound.mode === "global" ? qsTr("Global outbound") : qsTr("Default rule outbound");
        const detail = outbound.balanced ? qsTr("Load balancing · node varies by connection")
                     : !outbound.node ? qsTr("Determined by matching rules") : "";
        return heading + (path ? ": " + path : "") + (detail ? " · " + detail : "")
             + (!outbound.confirmed ? " · " + qsTr("Pending engine confirmation") : "");
    }
    color: theme.text
    wrapMode: Text.WrapAnywhere
    textFormat: Text.PlainText
}
