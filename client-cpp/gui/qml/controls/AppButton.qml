import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Button {
    id: root
    property string variant: "secondary"
    property string iconSource: ""
    property bool accent: variant === "primary"
    property bool danger: variant === "danger"
    property bool compact: false

    implicitHeight: compact ? 34 : 40
    implicitWidth: compact ? 76 : 120
    padding: compact ? Theme.spacingS + 2 : Theme.spacingM + 2

    contentItem: RowLayout {
        spacing: iconSource.length > 0 ? Theme.spacingS : 0
        Image {
            visible: root.iconSource.length > 0
            source: root.iconSource
            sourceSize.width: Theme.fontBodyLarge
            sourceSize.height: Theme.fontBodyLarge
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            Layout.fillWidth: true
            text: root.text
            color: !root.enabled ? Theme.secondaryText : Theme.primaryText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font.pixelSize: compact ? Theme.fontCaption : Theme.fontBody
            font.weight: Font.DemiBold
        }
    }

    background: Rectangle {
        radius: Theme.radiusMedium
        color: !root.enabled
               ? Qt.rgba(0.20, 0.22, 0.26, 0.28)
               : root.pressed
                 ? (root.danger ? Qt.rgba(0.82, 0.28, 0.36, 0.62)
                                : Qt.rgba(0.96, 0.98, 1.0, root.accent ? 0.42 : 0.30))
                 : root.hovered
                   ? (root.danger ? Qt.rgba(0.88, 0.34, 0.42, 0.50)
                                  : Qt.rgba(0.96, 0.98, 1.0, root.accent ? 0.30 : 0.22))
                   : (root.danger ? Qt.rgba(0.55, 0.20, 0.27, 0.28)
                                  : Qt.rgba(0.90, 0.93, 0.97, root.accent ? 0.18 : 0.10))
        border.color: !root.enabled
                      ? Theme.borderSoft
                      : root.danger ? Qt.rgba(1.0, 0.58, 0.64, root.hovered ? 0.70 : 0.36)
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

        Behavior on color { ColorAnimation { duration: Theme.animationNormal; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: Theme.animationNormal; easing.type: Easing.OutCubic } }
    }

    scale: root.pressed ? 0.975 : root.hovered ? 1.01 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.animationFast; easing.type: Easing.OutCubic } }
}
