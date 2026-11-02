pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ComboBox {
    id: control
    required property ThemeTokens theme
    // Display labels may include shared runtime state; activation still uses raw names.
    property var optionLabels: []
    property var optionDescriptions: []
    property var optionDelays: []
    property bool delaysStale: false
    readonly property var currentDelay: currentIndex >= 0 && optionDelays.length > currentIndex ? optionDelays[currentIndex] : ({})
    readonly property string currentLabel: currentIndex >= 0 && optionLabels.length > currentIndex ? optionLabels[currentIndex] : currentText
    readonly property string defaultDisplayText: currentLabel + (currentDelay.hasDelay ? " · " + currentDelay.text : "")
    displayText: defaultDisplayText
    font.family: theme.fontFamily
    font.pixelSize: theme.bodySize
    implicitHeight: theme.controlHeight
    leftPadding: 12
    rightPadding: 30
    activeFocusOnTab: true
    contentItem: RowLayout {
        spacing: 12
        Text {
            text: control.displayText === control.defaultDisplayText ? control.currentLabel : control.displayText
            font: control.font
            color: control.enabled ? control.theme.text : control.theme.muted
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }
        DelayLabel {
            theme: control.theme
            visible: !!control.currentDelay.hasDelay
            text: control.currentDelay.text || ""
            result: control.currentDelay.result || ""
            stale: control.delaysStale
            Layout.minimumWidth: implicitWidth
        }
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
        readonly property string nameLabel: control.optionLabels.length > index ? control.optionLabels[index] : control.textRole ? model[control.textRole] : model.modelData
        readonly property var delay: control.optionDelays.length > index ? control.optionDelays[index] : ({})
        text: nameLabel + (delay.hasDelay ? " · " + delay.text : "")
        Accessible.description: control.optionDescriptions.length > index ? control.optionDescriptions[index] : ""
        ThemedToolTip { theme: control.theme; visible: entry.hovered && text.length > 0; text: control.optionDescriptions.length > entry.index ? control.optionDescriptions[entry.index] : "" }
        font: control.font
        contentItem: RowLayout {
            spacing: 12
            Text {
                text: entry.nameLabel
                font: entry.font
                color: entry.highlighted ? control.theme.onSelection : control.theme.text
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            DelayLabel {
                theme: control.theme
                visible: !!entry.delay.hasDelay
                text: entry.delay.text || ""
                result: entry.delay.result || ""
                stale: control.delaysStale
                Layout.minimumWidth: implicitWidth
            }
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
