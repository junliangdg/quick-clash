pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QuickClash.Ui

Item {
    id: root
    required property ThemeTokens theme
    required property RuntimeListModel listModel
    property var columns: []
    property var cellText: function(entry, column) { return ""; }
    property var supportingText: function(entry, column) { return ""; }
    property var describe: function(entry) { return JSON.stringify(entry); }
    property var cellColor: function(entry, column) { return root.theme.text; }
    property int minimumWidth: theme.scaledSize(700)
    property int rowHeight: theme.rowHeight
    property string actionText: qsTr("Details")
    property bool actionsEnabled: true
    property bool activateRow: true
    property bool showRowDescription: false
    property string selectedKey: ""
    property bool expandable: false
    property string expandedKey: ""
    property bool followTail: false
    property bool userFollowing: true
    property string emptyText: qsTr("No entries")
    readonly property bool horizontalOverflow: viewport.contentWidth > viewport.width + 0.5
    readonly property real totalWeight: columns.reduce(function(sum, column) { return sum + column.weight; }, 0) || 1
    readonly property string query: listModel.query
    signal invoked(var entry)
    function columnWidth(index): real { return (viewport.contentWidth - (actionText ? root.theme.scaledSize(76) : 0)) * columns[index].weight / totalWeight; }
    function activate(entry, key): void {
        if (expandable)
            expandedKey = expandedKey === key ? "" : key;
        else
            invoked(entry);
    }
    function jumpToLatest(): void {
        userFollowing = true;
        list.positionViewAtEnd();
    }
    onFollowTailChanged: { if (followTail) { userFollowing = true; tail.restart(); } }
    onListModelChanged: expandedKey = ""
    Component.onCompleted: { if (followTail) tail.restart(); }
    Timer { id: tail; interval: 0; onTriggered: { if (root.followTail && root.userFollowing) list.positionViewAtEnd(); } }
    Connections {
        target: root.listModel
        function onCountChanged(): void { if (root.followTail && root.userFollowing) tail.restart(); }
    }
    FlatTile { anchors.fill: parent; theme: root.theme }
    Flickable {
        id: viewport
        anchors.fill: parent
        anchors.margins: 1
        clip: true
        contentWidth: Math.max(width, root.minimumWidth)
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar {}
        Column {
            width: viewport.contentWidth
            height: viewport.height - (viewport.contentWidth > viewport.width ? 12 : 0)
            Rectangle {
                width: parent.width
                height: root.theme.scaledSize(32)
                color: root.theme.inset
                Row {
                    anchors.fill: parent
                    Repeater {
                        model: root.columns
                        TableCell {
                            required property var modelData
                            required property int index
                            theme: root.theme
                            width: root.columnWidth(index)
                            height: parent.height
                            text: modelData.title
                            numeric: modelData.numeric === true
                            heading: true
                        }
                    }
                    TableCell {
                        visible: root.actionText.length > 0
                        width: root.theme.scaledSize(76)
                        height: parent.height
                        theme: root.theme
                        text: root.actionText
                        heading: true
                    }
                }
            }
            ListView {
                id: list
                objectName: "runtime_view"
                width: parent.width
                height: parent.height - y
                clip: true
                reuseItems: true
                model: root.listModel
                boundsBehavior: Flickable.StopAtBounds
                onMovementStarted: root.userFollowing = false
                onMovementEnded: root.userFollowing = atYEnd
                ScrollBar.vertical: ScrollBar { x: Math.min(viewport.width, list.width) - width + viewport.contentX }
                delegate: ItemDelegate {
                    id: tile
                    required property var entry
                    required property string stableKey
                    readonly property bool expanded: root.expandable && root.expandedKey === stableKey
                    width: list.width
                    height: root.rowHeight + (expanded ? expandedText.implicitHeight + 20 : 0)
                    padding: 0
                    hoverEnabled: true
                    Accessible.name: root.describe(entry)
                    activeFocusOnTab: true
                    HoverHandler { cursorShape: root.activateRow ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    onClicked: { if (root.activateRow) root.activate(entry, stableKey); }
                    Keys.onReturnPressed: { if (root.activateRow) root.activate(entry, stableKey); }
                    ThemedToolTip {
                        theme: root.theme
                        width: Math.max(0, Math.min(root.theme.scaledSize(520), viewport.width - 24))
                        visible: root.showRowDescription && (tile.hovered || tile.activeFocus)
                        text: visible ? root.describe(tile.entry) : ""
                    }
                    background: StateSurface {
                        theme: root.theme
                        focused: tile.activeFocus
                        insetFocus: true
                        focusClipLeft: viewport.contentX
                        focusClipRight: Math.max(0, width - viewport.width - viewport.contentX)
                        color: root.theme.stateColor(root.selectedKey === tile.stableKey, tile.hovered, tile.down)
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.theme.border }
                    }
                    contentItem: Item {
                        Row {
                            width: parent.width
                            height: root.rowHeight
                            Repeater {
                                model: root.columns
                                TableCell {
                                    required property int index
                                    required property var modelData
                                    theme: root.theme
                                    width: root.columnWidth(index)
                                    height: root.rowHeight
                                    text: root.cellText(tile.entry, index)
                                    supporting: root.supportingText(tile.entry, index)
                                    numeric: modelData.numeric === true
                                    foreground: root.selectedKey === tile.stableKey ? root.theme.onSelection : root.cellColor(tile.entry, index)
                                }
                            }
                            ActionButton {
                                visible: root.actionText.length > 0
                                theme: root.theme
                                width: root.theme.scaledSize(76)
                                height: root.rowHeight
                                text: root.actionText
                                enabled: root.actionsEnabled
                                ghost: true
                                insetFocus: true
                                onActiveFocusChanged: {
                                    if (activeFocus)
                                        viewport.contentX = Math.max(0, viewport.contentWidth - viewport.width);
                                }
                                Accessible.name: root.actionText + " · " + root.describe(tile.entry)
                                onClicked: root.activate(tile.entry, tile.stableKey)
                            }
                        }
                        TextArea {
                            id: expandedText
                            visible: tile.expanded
                            x: 10
                            y: root.rowHeight
                            width: Math.max(0, Math.min(viewport.width, tile.width) - 20)
                            text: tile.expanded ? root.describe(tile.entry) : ""
                            textFormat: TextEdit.PlainText
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.bodySize
                            color: root.theme.text
                            background: Item {}
                        }
                    }
                }
            }
        }
    }
    Label {
        anchors.centerIn: parent
        visible: root.listModel.count === 0
        text: root.emptyText
        color: root.theme.muted
        font.pixelSize: root.theme.bodySize
        width: Math.max(0, parent.width - 32)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }
}
