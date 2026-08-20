pragma Singleton
import QtQuick
import QtQml

QtObject {
    // Color tokens
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

    // Spacing tokens
    readonly property int spacingXS: 4
    readonly property int spacingS: 8
    readonly property int spacingM: 12
    readonly property int spacingL: 16
    readonly property int spacingXL: 24
    readonly property int spacingXXL: 32

    // Radius tokens
    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 16
    readonly property int radiusRound: 999

    // Typography tokens
    readonly property int fontCaption: 12
    readonly property int fontBody: 13
    readonly property int fontBodyLarge: 15
    readonly property int fontTitle: 20
    readonly property int fontHeading: 28

    // Animation tokens
    readonly property int animationFast: 100
    readonly property int animationNormal: 180
    readonly property int animationSlow: 260
}
