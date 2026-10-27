pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QuickClash.Ui

ColumnLayout {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    required property string pageId
    required property ReplayModel replayModel
    required property string stream
    property bool active: ui.uiVisible && visible
    property var saved: ({})
    property string retainedFilter: ""
    property real retainedPosition: 0
    spacing: 12
    function syncDemand(): void {
        ui.setDemand(pageId + ":list", stream, active);
    }
    onActiveChanged: syncDemand()
    Component.onCompleted: {
        saved = ui.readState(pageId);
        filter.text = saved.filter || "";
        restorePosition.start();
        syncDemand();
    }
    Component.onDestruction: {
        // Destruction order of child objects is unspecified; retain only scalar state here.
        ui.setFilter(pageId, retainedFilter);
        ui.saveState(pageId, {
            "filter": retainedFilter,
            "position": retainedPosition
        });
        ui.setDemand(pageId + ":list", stream, false);
    }
    RowLayout {
        Layout.fillWidth: true
        ThemedField {
            theme: root.theme
            id: filter
            objectName: "list_filter"
            font.family: root.theme.fontFamily
            Layout.fillWidth: true
            placeholderText: qsTr("Filter replay data…")
            color: root.theme.text
            placeholderTextColor: root.theme.muted
            maximumLength: 128
            selectByMouse: true
            onTextEdited: debounce.restart()
            onTextChanged: root.retainedFilter = text
            Accessible.name: qsTr("Filter list")
        }
        ActionButton {
            theme: root.theme
            text: root.ui.replay ? qsTr("Stop replay") : qsTr("Load replay")
            onClicked: root.ui.replay = !root.ui.replay
        }
    }
    Connections {
        target: root.ui
        function onFilterChanged(pageId: string, text: string): void {
            if (pageId === root.pageId)
                filter.text = text;
        }
    }
    Timer {
        id: restorePosition
        interval: 0
        onTriggered: rows.contentY = Math.max(0, Math.min(Number(root.saved.position || 0), rows.contentHeight - rows.height))
    }
    Timer {
        id: debounce
        interval: 150
        onTriggered: root.ui.setFilter(root.pageId, filter.text)
    }
    Label {
        font.family: root.theme.fontFamily
        text: root.ui.replay ? qsTr("%1 synthetic records · No network requests · Visible rows are reused").arg(root.replayModel.count) : qsTr("No live data is available here yet. Load replay to preview the list.")
        color: root.theme.muted
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
    FlatTile {
        theme: root.theme
        Layout.fillWidth: true
        Layout.fillHeight: true
        ListView {
            id: rows
            anchors.fill: parent
            anchors.margins: 8
            clip: true
            reuseItems: true
            model: root.replayModel
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            onContentYChanged: root.retainedPosition = contentY
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                font.family: root.theme.fontFamily
                required property int index
                required property string name
                required property string detail
                required property string status
                required property string metric
                width: ListView.view.width
                height: 62
                highlighted: ListView.isCurrentItem
                onClicked: rows.currentIndex = index
                background: StateSurface {
                    theme: root.theme
                    selected: row.highlighted
                    hovered: row.hovered
                    down: row.down
                    focused: row.activeFocus
                }
                contentItem: RowLayout {
                    spacing: 16
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            font.family: root.theme.fontFamily
                            text: row.name
                            color: row.highlighted ? root.theme.onSelection : root.theme.text
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Label {
                            font.family: root.theme.fontFamily
                            text: row.detail
                            color: row.highlighted ? root.theme.onSelection : root.theme.muted
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: row.status
                        color: row.highlighted ? root.theme.onSelection : root.theme.muted
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: row.metric
                        color: root.theme.accent
                        Layout.preferredWidth: 80
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
            Label {
                font.family: root.theme.fontFamily
                anchors.centerIn: parent
                visible: root.replayModel.count === 0
                text: root.ui.replay ? qsTr("No matching records") : qsTr("No data")
                color: root.theme.muted
            }
        }
    }
}
