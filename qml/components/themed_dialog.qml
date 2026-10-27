pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QuickClash.Ui

Dialog {
    id: control
    required property ThemeTokens theme
    padding: theme.panelPadding
    font.family: theme.fontFamily
    font.pixelSize: theme.bodySize
    header: Label {
        text: control.title
        visible: text.length > 0
        padding: control.theme.panelPadding
        font.weight: Font.DemiBold
        font.pixelSize: 16
        color: control.theme.text
        wrapMode: Text.WordWrap
    }
    footer: DialogButtonBox {
        visible: count > 0
        padding: control.theme.panelPadding
        spacing: 8
        standardButtons: control.standardButtons
        delegate: ActionButton {
            theme: control.theme
            prominent: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
        }
        background: Item {}
    }
    background: Rectangle {
        radius: control.theme.panelRadius
        color: control.theme.surface
        border.width: 1
        border.color: control.theme.border
    }
    Overlay.modal: Rectangle {
        color: control.theme.scrim
    }
}
