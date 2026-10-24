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
        TextField {
            id: filter
            objectName: "list_filter"
            font.family: root.theme.fontFamily
            Layout.fillWidth: true
            placeholderText: qsTr("筛选回放数据…")
            color: root.theme.text
            placeholderTextColor: root.theme.muted
            maximumLength: 128
            selectByMouse: true
            background: Rectangle {
                radius: 10
                color: root.theme.surface
                border.width: filter.activeFocus ? 2 : 1
                border.color: filter.activeFocus ? root.theme.accent : root.theme.border
            }
            onTextEdited: debounce.restart()
            onTextChanged: root.retainedFilter = text
            Accessible.name: qsTr("筛选列表")
        }
        ActionButton {
            theme: root.theme
            text: root.ui.replay ? qsTr("停止回放") : qsTr("加载回放")
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
        text: root.ui.replay ? qsTr("%1 条合成记录 · 无网络请求 · 列表按可见区域复用").arg(root.replayModel.count) : qsTr("尚未接入真实内核；可加载回放检查列表与布局。")
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
                background: Rectangle {
                    radius: 10
                    color: row.hovered || row.highlighted ? root.theme.accentSurface : "transparent"
                    border.width: row.activeFocus ? 1 : 0
                    border.color: root.theme.accent
                }
                contentItem: RowLayout {
                    spacing: 16
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            font.family: root.theme.fontFamily
                            text: row.name
                            color: root.theme.text
                            font.bold: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Label {
                            font.family: root.theme.fontFamily
                            text: row.detail
                            color: root.theme.muted
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                    Label {
                        font.family: root.theme.fontFamily
                        text: row.status
                        color: root.theme.muted
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
                text: root.ui.replay ? qsTr("没有匹配记录") : qsTr("暂无数据")
                color: root.theme.muted
            }
        }
    }
}
