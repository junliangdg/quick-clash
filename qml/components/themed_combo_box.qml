pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QuickClash.Ui

ComboBox {
    id: control
    required property ThemeTokens theme
    font.family: theme.fontFamily
    font.pixelSize: theme.bodySize
    implicitHeight: theme.controlHeight
    leftPadding: 12
    rightPadding: 30
    activeFocusOnTab: true
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? control.theme.text : control.theme.muted
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: control.width - width - 10
        y: (control.height - height) / 2
        text: "▾"
        font.pixelSize: control.theme.scaledSize(14)
        color: control.theme.muted
    }
    background: StateSurface {
        theme: control.theme
        color: !control.enabled ? theme.inset : control.down ? theme.pressed : control.hovered ? theme.hover : theme.surface
        border.width: 1
        border.color: theme.controlOutline
        focused: control.activeFocus
    }
    delegate: ItemDelegate {
        id: entry
        required property int index
        required property var model
        width: ListView.view.width
        height: control.theme.rowHeight
        highlighted: control.highlightedIndex === index
        text: control.textRole ? model[control.textRole] : model.modelData
        font: control.font
        contentItem: Text {
            text: entry.text
            font: entry.font
            color: entry.highlighted ? control.theme.onSelection : control.theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: StateSurface {
            theme: control.theme
            selected: entry.highlighted
            hovered: entry.hovered
            down: entry.down
        }
    }
    popup: Popup {
        y: control.height + 6
        width: control.width
        padding: 6
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 280)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: ScrollBar {}
        }
        background: Rectangle {
            radius: control.theme.controlRadius
            color: control.theme.surface
            border.width: 1
            border.color: control.theme.border
        }
    }
}
