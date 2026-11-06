import QtQuick

QtObject {
    required property bool dark
    property string fontFamily: "sans-serif"
    property real fontPixelSize: 14
    readonly property real textScale: Math.max(1, Math.min(225 / 100, fontPixelSize / 14))
    function scaledSize(size: real): int {
        return Math.round(size * textScale);
    }
    readonly property color background: dark ? "#101114" : "#FAFAFB"
    readonly property color sidebar: dark ? "#15161A" : "#F4F4F6"
    readonly property color surface: dark ? "#191A20" : "#FFFFFF"
    readonly property color inset: dark ? "#22242C" : "#F2F2F5"
    readonly property color text: dark ? "#EDEDF2" : "#202024"
    readonly property color muted: dark ? "#A8A9B8" : "#62626E"
    readonly property color accent: dark ? "#B0A5FF" : "#5B4BC4"
    readonly property color accentForeground: dark ? "#251A58" : "#FFFFFF"
    readonly property color accentSurface: dark ? "#312A4F" : "#EEEBFD"
    readonly property color onSelection: dark ? "#C9BFFF" : "#5340AB"
    readonly property color upload: dark ? "#D5AE72" : "#936025"
    readonly property color info: dark ? "#8CC8F5" : "#28618E"
    readonly property color download: accent
    readonly property color border: dark ? "#323440" : "#E5E5EA"
    readonly property color controlOutline: dark ? "#818496" : "#858591"
    readonly property color success: dark ? "#93D4AC" : "#246944"
    readonly property color successSurface: dark ? "#1D3228" : "#EAF5EE"
    readonly property color warning: dark ? "#EBC47E" : "#855510"
    readonly property color warningSurface: dark ? "#382D1C" : "#FFF4DF"
    readonly property color error: dark ? "#FFA0B0" : "#B33043"
    readonly property color errorSurface: dark ? "#3B222C" : "#FFF0F2"
    readonly property color hover: dark ? "#2A2C36" : "#EAEAF0"
    readonly property color pressed: dark ? "#343743" : "#E0DFE8"
    readonly property color primaryHover: dark ? "#BDB3FF" : "#5141B2"
    readonly property color primaryPressed: dark ? "#A195F0" : "#46369F"
    readonly property color selectedHover: dark ? "#3B335F" : "#E5DFFB"
    readonly property color selectedPressed: dark ? "#453B6D" : "#DAD2F6"
    readonly property color errorHover: dark ? "#4A2A35" : "#FBE3E8"
    readonly property color errorPressed: dark ? "#59323F" : "#F8D8DF"
    readonly property color scrim: dark ? "#8F000000" : "#5C000000"
    readonly property int panelRadius: 10
    readonly property int controlRadius: 7
    readonly property int panelPadding: 16
    readonly property int scrollBarWidth: 10
    readonly property int gap: 16
    readonly property int controlHeight: scaledSize(36)
    readonly property int compactHeight: scaledSize(32)
    readonly property int rowHeight: scaledSize(40)
    readonly property int bodySize: scaledSize(14)
    readonly property int navigationSize: scaledSize(15)
    readonly property int supportingSize: scaledSize(12)
    readonly property int titleSize: scaledSize(24)
    readonly property int focusWidth: 2
    readonly property int focusGap: 2
    readonly property int focusExtent: focusGap + focusWidth
    readonly property int focusMargin: focusExtent + scaledSize(4)
    function delayColor(result): color {
        if (result === "Failed" || result === "Timeout")
            return error;
        if (result === "Testing" || result === "Queued")
            return warning;
        const value = parseInt(result);
        if (isNaN(value) || value < 0)
            return muted;
        return value < 300 ? success : value < 800 ? warning : error;
    }
    function stateColor(selected, hovered, down): color {
        return selected ? (down ? selectedPressed : hovered ? selectedHover : accentSurface) : (down ? pressed : hovered ? hover : "transparent");
    }
}
