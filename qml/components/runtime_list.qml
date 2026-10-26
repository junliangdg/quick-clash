pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

Item {
    id: root
    required property ThemeTokens theme
    required property RuntimeListModel listModel
    property string heading: ""
    property string actionText: ""
    property var describe: function(entry) { return JSON.stringify(entry); }
    readonly property string query: listModel.query
    property bool followTail: false
    property bool userFollowing: true
    onFollowTailChanged: { if (followTail) { userFollowing = true; if (tail) tail.restart(); } }
    onListModelChanged: { if (debounce && search) { debounce.stop(); search.text = listModel.query; } }
    Component.onCompleted: { if (followTail) tail.restart(); }
    signal invoked(var entry)
    Timer { id: debounce; interval: 150; onTriggered: root.listModel.query = search.text }
    Timer { id: tail; interval: 0; onTriggered: { if (root.followTail && root.userFollowing) list.positionViewAtEnd(); } }
    Connections {
        target: root.listModel
        function onCountChanged(): void { if (root.followTail && root.userFollowing) tail.restart(); }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Label {
            text: root.heading + " · " + root.listModel.count
            color: root.theme.text
            font.pixelSize: 20
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        TextField {
            id: search
            objectName: "runtime_filter"
            text: root.listModel.query
            placeholderText: qsTr("Filter")
            maximumLength: 128
            onTextChanged: debounce.restart()
            Layout.fillWidth: true
        }
        Label {
            visible: root.listModel.count === 0
            text: qsTr("No entries")
            color: root.theme.muted
        }
        ListView {
            id: list
            objectName: "runtime_view"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            reuseItems: true
            model: root.listModel
            onMovementStarted: root.userFollowing = false
            onMovementEnded: root.userFollowing = atYEnd
            ScrollBar.vertical: ScrollBar {}
            delegate: FlatTile {
                id: tile
                required property var entry
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
                        text: root.describe(tile.entry)
                        color: root.theme.text
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                    }
                    ActionButton {
                        theme: root.theme
                        text: root.actionText
                        visible: text.length > 0
                        onClicked: root.invoked(tile.entry)
                    }
                }
            }
        }
    }
}
