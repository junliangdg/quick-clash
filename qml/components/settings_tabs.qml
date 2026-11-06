pragma ComponentBehavior: Bound
import QtQuick
import QuickClash.Ui

Item {
    id: root
    required property ThemeTokens theme
    required property UiState ui
    property int currentIndex: ui.settingsSession.category || 0
    property int focusIndex: currentIndex
    readonly property var labels: [qsTr("General"), qsTr("Network"), qsTr("Core & Geo"), qsTr("Data"), qsTr("About")]
    readonly property var names: [labels[0], labels[1], qsTr("Core and resources"), qsTr("Data and diagnostics"), labels[4]]
    readonly property bool overflowing: tabRow.width > width - 2 * theme.focusMargin + 0.5
    readonly property int arrowWidth: 48
    // Focus frames are inset; secondary navigation needs no vertical focus gutter.
    implicitHeight: theme.compactHeight
    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Settings categories")
    objectName: "settings_tabs"
    function ensureVisible(index): void {
        // Selection changes font weight and widths; Row positioning is polished later.
        tabRow.forceLayout();
        const button = tabs.itemAt(index);
        if (!button)
            return;
        // Tab focus frames are inset, so the full label fits even at the minimum width.
        const left = button.x;
        const right = button.x + button.width;
        let next = strip.contentX;
        if (left < next)
            next = left;
        else if (right > next + strip.width)
            next = right - strip.width;
        strip.contentX = Math.max(0, Math.min(next, Math.max(0, strip.contentWidth - strip.width)));
    }
    function moveFocus(index): void {
        focusIndex = Math.max(0, Math.min(4, index));
        ensureVisible(focusIndex);
        tabs.itemAt(focusIndex).forceActiveFocus(Qt.TabFocusReason);
    }
    function revealTab(): void {
        const focused = tabs.itemAt(focusIndex);
        ensureVisible(focused && focused.activeFocus ? focusIndex : currentIndex);
    }
    onCurrentIndexChanged: {
        focusIndex = currentIndex;
        revealLater.restart();
    }
    onWidthChanged: revealLater.restart()
    Timer {
        id: revealLater
        interval: 0
        onTriggered: root.revealTab()
    }
    ActionButton {
        objectName: "settings_tabs_previous"
        theme: root.theme
        text: "‹"
        Accessible.name: qsTr("Scroll settings categories backward")
        visible: root.overflowing
        enabled: strip.contentX > 0.5
        x: root.theme.focusMargin
        y: 0
        width: root.arrowWidth
        height: root.height
        insetFocus: true
        activeFocusOnTab: false
        onClicked: strip.contentX = Math.max(0, strip.contentX - strip.width * 0.7)
    }
    Flickable {
        id: strip
        objectName: "settings_tabs_viewport"
        x: root.theme.focusMargin + (root.overflowing ? root.arrowWidth : 0)
        width: Math.max(0, root.width - 2 * root.theme.focusMargin - (root.overflowing ? 2 * root.arrowWidth : 0))
        height: root.height
        contentWidth: tabRow.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Row {
            id: tabRow
            x: 0
            y: 0
            spacing: 4
            onWidthChanged: revealLater.restart()
            Repeater {
                id: tabs
                model: root.labels
                delegate: ActionButton {
                    id: tab
                    required property int index
                    required property string modelData
                    objectName: "settings_tab_" + index
                    theme: root.theme
                    text: modelData + (index === 1 && root.ui.settingsSession.hasDraft ? " •" : "")
                    Accessible.role: Accessible.PageTab
                    Accessible.selected: selected
                    Accessible.name: root.names[index]
                    Accessible.description: index === 1 && root.ui.settingsSession.hasDraft ? qsTr("Contains unapplied changes") : ""
                    selected: root.currentIndex === index
                    insetFocus: true
                    width: implicitWidth
                    height: root.height
                    activeFocusOnTab: root.focusIndex === index || activeFocus
                    onClicked: {
                        root.focusIndex = index;
                        root.ui.setSettingsCategory(index);
                    }
                    onActiveFocusChanged: if (activeFocus)
                        root.ensureVisible(index)
                    Keys.priority: Keys.BeforeItem
                    Keys.onLeftPressed: root.moveFocus(index - 1)
                    Keys.onRightPressed: root.moveFocus(index + 1)
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Home) {
                            root.moveFocus(0);
                            event.accepted = true;
                        } else if (event.key === Qt.Key_End) {
                            root.moveFocus(4);
                            event.accepted = true;
                        }
                    }
                }
            }
        }
    }
    ActionButton {
        objectName: "settings_tabs_next"
        theme: root.theme
        text: "›"
        Accessible.name: qsTr("Scroll settings categories forward")
        visible: root.overflowing
        enabled: strip.contentX < strip.contentWidth - strip.width - 0.5
        x: root.width - root.theme.focusMargin - width
        y: 0
        width: root.arrowWidth
        height: root.height
        insetFocus: true
        activeFocusOnTab: false
        onClicked: strip.contentX = Math.min(strip.contentWidth - strip.width, strip.contentX + strip.width * 0.7)
    }
}
