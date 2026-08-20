pragma Singleton
import QtQuick
import QtQml

QtObject {
    readonly property color background: "#101318"
    readonly property color surface: Qt.rgba(0.14, 0.16, 0.19, 0.82)
    readonly property color panel: Qt.rgba(0.16, 0.18, 0.22, 0.78)
    readonly property color panelRaised: Qt.rgba(0.22, 0.24, 0.28, 0.84)
    readonly property color surfaceRaised: "#30343b"
    readonly property color surfaceHover: Qt.rgba(0.88, 0.91, 0.96, 0.16)
    readonly property color card: Qt.rgba(0.18, 0.20, 0.24, 0.88)
    readonly property color border: Qt.rgba(0.88, 0.91, 0.96, 0.26)
    readonly property color borderSoft: Qt.rgba(0.88, 0.91, 0.96, 0.12)
    readonly property color glassHighlight: Qt.rgba(0.96, 0.98, 1.0, 0.34)
    readonly property color glowBlue: Qt.rgba(0.40, 0.52, 0.66, 0.10)
    readonly property color glowViolet: Qt.rgba(0.42, 0.44, 0.50, 0.08)
    readonly property color primaryText: "#f4f5f6"
    readonly property color secondaryText: "#aeb4bf"
    readonly property color accent: "#dce4ee"
    readonly property color accentPressed: "#bac7d5"
    readonly property color accentSoft: Qt.rgba(0.88, 0.92, 0.97, 0.18)
    readonly property color otherBubble: Qt.rgba(0.18, 0.20, 0.24, 0.90)
    readonly property color selfBubble: Qt.rgba(0.68, 0.76, 0.86, 0.30)
    readonly property color success: "#5bd49a"
    readonly property color danger: "#ef7d8a"
}
