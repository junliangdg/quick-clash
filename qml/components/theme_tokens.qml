import QtQuick

QtObject {
    required property bool dark
    property string fontFamily: "sans-serif"
    readonly property color background: dark ? "#181a20" : "#f4f5f9"
    readonly property color sidebar: dark ? "#202229" : "#eaeaf0"
    readonly property color surface: dark ? "#252830" : "#ffffff"
    readonly property color inset: dark ? "#30343e" : "#f0f1f6"
    readonly property color text: dark ? "#edf0f7" : "#202533"
    readonly property color muted: dark ? "#aab2c3" : "#687184"
    readonly property color accent: dark ? "#76aaff" : "#1767d8"
    readonly property color accentSurface: dark ? "#263b58" : "#e5efff"
    readonly property color border: dark ? "#3b414e" : "#e1e5ed"
    readonly property color success: dark ? "#9edcb2" : "#246d42"
    readonly property color warning: dark ? "#f4c783" : "#82561a"
}
