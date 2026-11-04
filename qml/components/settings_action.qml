pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QuickClash.Ui

ActionButton {
    id: control
    implicitHeight: Math.max(theme.controlHeight, implicitContentHeight + 16)
    contentItem: Label {
        text: control.text
        font: control.font
        color: control.foregroundColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
