import QtQuick
import QtQuick.Controls
import QuickClash.Ui

TextArea {
    id: control
    required property ThemeTokens theme
    property bool invalid: false
    property bool tabInsertsText: false
    Accessible.name: placeholderText
    activeFocusOnTab: true
    textFormat: TextEdit.PlainText
    Keys.onPressed: event => {
        if (event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab)
            return;
        if (control.tabInsertsText && !(event.modifiers & Qt.ControlModifier))
            return;
        const forward = event.key !== Qt.Key_Backtab && !(event.modifiers & Qt.ShiftModifier);
        let next = control.nextItemInFocusChain(forward);
        while (next && next !== control && (!next.activeFocusOnTab || !next.visible || !next.enabled))
            next = next.nextItemInFocusChain(forward);
        if (next && next !== control)
            next.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason);
        event.accepted = true;
    }
    font.family: theme.fontFamily
    font.pixelSize: theme.bodySize
    padding: 12
    selectByMouse: true
    color: enabled ? theme.text : theme.muted
    placeholderTextColor: theme.muted
    selectionColor: theme.accent
    selectedTextColor: theme.accentForeground
    background: StateSurface {
        theme: control.theme
        color: control.enabled ? theme.surface : theme.inset
        border.width: 1
        border.color: control.invalid ? theme.error : theme.controlOutline
        focused: control.activeFocus
    }
}
