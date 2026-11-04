pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    required property ThemeTokens theme
    property string title
    property string description
    property bool inlineControl: false
    property bool stackedControl: false
    property int controlWidth: 240
    readonly property bool wide: !stackedControl && width >= theme.scaledSize(560)
    default property alias controls: slot.data
    implicitHeight: layout.implicitHeight + 24
    GridLayout {
        id: layout
        x: 0
        y: 12
        width: root.width
        columns: root.wide || root.inlineControl ? 2 : 1
        columnSpacing: 16
        rowSpacing: 8
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 4
            Label {
                text: root.title
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.bodySize
                color: root.theme.text
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: text.length > 0
                text: root.description
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.supportingSize
                color: root.theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        Item {
            id: slot
            Layout.fillWidth: !root.wide && !root.inlineControl
            Layout.preferredWidth: root.controlWidth
            Layout.maximumWidth: root.wide || root.inlineControl ? root.controlWidth : Number.POSITIVE_INFINITY
            Layout.minimumWidth: 0
            implicitHeight: childrenRect.height
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: root.theme.controlOutline
        opacity: 0.4
    }
}
