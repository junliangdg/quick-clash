import QtQuick
import QuickClash.Ui

Loader {
    id: root
    required property UiState ui
    required property ThemeTokens theme
    required property string contentId
    required property string instanceKey
    property var context: ({})
    property bool initialized: false
    onContentIdChanged: if (initialized)
        loadContent()
    onContextChanged: if (initialized)
        loadContent()
    onInstanceKeyChanged: if (initialized)
        loadContent()
    function loadContent(): void {
        const resolved = ui.embeddedSource(contentId, context);
        // Clear the old instance first, including when a new context is rejected.
        source = "";
        if (resolved.toString().length > 0) {
            setSource(resolved, {
                "ui": ui,
                "theme": theme,
                "instanceKey": instanceKey
            });
        }
    }
    Component.onCompleted: {
        initialized = true;
        loadContent();
    }
}
