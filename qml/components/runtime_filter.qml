import QtQuick
import QuickClash.Ui

ThemedField {
    id: root
    required property RuntimeListModel listModel
    objectName: "runtime_filter"
    Accessible.name: placeholderText
    placeholderText: qsTr("Filter")
    maximumLength: 128
    text: listModel.query
    onTextChanged: debounce.restart()
    onListModelChanged: { debounce.stop(); text = listModel.query; }
    Timer { id: debounce; interval: 150; onTriggered: root.listModel.query = root.text }
}
