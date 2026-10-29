pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QuickClash.Ui

Dialog {
    id: control
    required property ThemeTokens theme
    property Item initialFocusItem: null
    property Item returnFocusItem: null
    implicitWidth: Math.min(520, parent ? Math.max(0, parent.width - 32) : 520)
    focus: true
    Connections {
        target: control
        function onAboutToShow(): void {
            const owner = control.parent ? control.parent.Window.window : null;
            control.returnFocusItem = owner ? owner.activeFocusItem : null;
        }
        function onOpened(): void {
            const safeButton = control.initialFocusItem || control.standardButton(Dialog.Cancel) || control.standardButton(Dialog.Close);
            if (safeButton && safeButton.visible && safeButton.enabled)
                safeButton.forceActiveFocus(Qt.PopupFocusReason);
        }
        function onClosed(): void {
            if (control.returnFocusItem && control.returnFocusItem.visible && control.returnFocusItem.enabled)
                control.returnFocusItem.forceActiveFocus(Qt.PopupFocusReason);
            control.returnFocusItem = null;
        }
    }
    padding: theme.panelPadding
    font.family: theme.fontFamily
    font.pixelSize: theme.bodySize
    header: Label {
        text: control.title
        visible: text.length > 0
        padding: control.theme.panelPadding
        font.weight: Font.DemiBold
        font.pixelSize: control.theme.scaledSize(16)
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
