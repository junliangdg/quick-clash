import QtQuick
import QtQuick.Controls.impl
import QuickClash.Ui

Item {
    id: root
    required property ThemeTokens theme
    property alias source: icon.source
    property alias color: icon.color
    implicitWidth: theme.scaledSize(18)
    implicitHeight: theme.scaledSize(18)
    Accessible.ignored: true
    IconImage {
        id: icon
        anchors.fill: parent
        sourceSize.width: width
        sourceSize.height: height
        color: root.theme.muted
    }
}
