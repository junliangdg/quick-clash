import QtQuick
import QtQuick.Controls
import QuickClash.Ui

Loader {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    // Only a page root can extend into the shell margin; nested dialogs remain contained.
    readonly property var loadedPage: item
    readonly property real pageLeftOutset: loadedPage && loadedPage.pageLeftOutset !== undefined ? loadedPage.pageLeftOutset : 0
    readonly property real pageRightOutset: loadedPage && loadedPage.pageRightOutset !== undefined ? loadedPage.pageRightOutset : 0
    function loadPage(): void {
        setSource(ui.pageSource, {
            "ui": ui,
            "theme": theme
        });
    }
    Component.onCompleted: loadPage()
    Label {
        font.family: root.theme.fontFamily
        anchors.centerIn: parent
        width: Math.max(0, parent.width - 32)
        visible: root.status === Loader.Error
        text: qsTr("Unable to load this page. Switch pages to retry; see diagnostics for details.")
        color: root.theme.warning
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }
    Connections {
        target: root.ui
        function onRouteChanged(): void {
            root.loadPage();
        }
    }
}
