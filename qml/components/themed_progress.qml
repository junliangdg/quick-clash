import QtQuick
import QtQuick.Controls
import QuickClash.Ui

ProgressBar {
    id: control
    required property ThemeTokens theme
    implicitHeight: 6
    background: Rectangle {
        implicitHeight: 6
        radius: 3
        color: control.theme.inset
    }
    contentItem: Item {
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 3
            color: control.theme.accent
        }
    }
}
