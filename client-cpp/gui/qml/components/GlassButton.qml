import QtQuick
import QtQuick.Controls
import LanChatGui

Button {
    id: root
    property bool accent: false
    property bool danger: false
    property bool compact: false

    implicitHeight: compact ? 34 : 40
    implicitWidth: compact ? 76 : 120
    padding: compact ? 10 : 14

    contentItem: Text {
        text: root.text
        color: !root.enabled ? Theme.secondaryText : Theme.primaryText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font.pixelSize: compact ? 12 : 13
        font.weight: Font.DemiBold
    }

    background: Rectangle {
        radius: 10
        color: !root.enabled
               ? Qt.rgba(0.16, 0.19, 0.25, 0.34)
               : root.pressed
                 ? (root.danger ? Qt.rgba(0.82, 0.28, 0.36, 0.62)
                                : root.accent ? Qt.rgba(0.96, 0.98, 1.0, 0.42)
                                              : Qt.rgba(0.90, 0.93, 0.97, 0.30))
                 : root.hovered
                   ? (root.danger ? Qt.rgba(0.88, 0.34, 0.42, 0.50)
                                  : root.accent ? Qt.rgba(0.96, 0.98, 1.0, 0.30)
                                                : Qt.rgba(0.90, 0.93, 0.97, 0.22))
                   : (root.danger ? Qt.rgba(0.55, 0.20, 0.27, 0.28)
                                  : root.accent ? Qt.rgba(0.96, 0.98, 1.0, 0.18)
                                                : Qt.rgba(0.90, 0.93, 0.97, 0.10))
        border.color: !root.enabled
                      ? Theme.borderSoft
                      : root.danger ? Qt.rgba(1.0, 0.58, 0.64, root.hovered ? 0.70 : 0.36)
                                    : root.accent ? Qt.rgba(0.96, 0.98, 1.0, root.hovered ? 0.86 : 0.52)
                                                  : Qt.rgba(0.92, 0.95, 1.0, root.hovered ? 0.56 : 0.24)
        border.width: root.activeFocus || root.hovered ? 1.5 : 1

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height * 0.46
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: root.enabled ? Theme.glassHighlight : "transparent" }
                GradientStop { position: 1.0; color: "transparent" }
            }
            opacity: root.hovered ? 0.95 : 0.58
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"
            border.color: root.accent && root.hovered ? Theme.glassHighlight : "transparent"
            border.width: 1
            opacity: 0.65
        }

        Behavior on color { ColorAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: 180; easing.type: Easing.OutCubic } }
    }

    scale: root.pressed ? 0.975 : root.hovered ? 1.01 : 1.0
    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
}
