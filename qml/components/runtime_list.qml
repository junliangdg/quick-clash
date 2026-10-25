pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    required property ThemeTokens theme
    property var entries: []
    property string heading: ""
    property string actionText: ""
    property var describe: function(entry) { return JSON.stringify(entry); }
    property var accept: function(entry) { return true; }
    property string query: ""
    property bool followTail: false
    property var compare: null
    property var filtered: entries.filter(function(entry) {
        return accept(entry) && (!query || describe(entry).toLowerCase().includes(query.toLowerCase()));
    }).sort(compare || function() { return 0; })
    onFilteredChanged: { if (followTail) Qt.callLater(function() { list.positionViewAtEnd(); }); }
    signal invoked(var entry)
    Timer { id: debounce; interval: 150; onTriggered: root.query = search.text }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Label {
            text: root.heading + " · " + root.filtered.length
            color: root.theme.text
            font.pixelSize: 20
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        TextField {
            id: search
            placeholderText: qsTr("Filter")
            maximumLength: 128
            onTextChanged: debounce.restart()
            Layout.fillWidth: true
        }
        Label {
            visible: root.filtered.length === 0
            text: qsTr("No entries")
            color: root.theme.muted
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            reuseItems: true
            model: root.filtered
            ScrollBar.vertical: ScrollBar {}
            delegate: FlatTile {
                id: tile
                required property var modelData
                width: list.width
                implicitHeight: content.implicitHeight + 28
                theme: root.theme
                ColumnLayout {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 14
                    Label {
                        text: root.describe(tile.modelData)
                        color: root.theme.text
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                    }
                    ActionButton {
                        theme: root.theme
                        text: root.actionText
                        visible: text.length > 0
                        onClicked: root.invoked(tile.modelData)
                    }
                }
            }
        }
    }
}
