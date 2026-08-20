import QtQuick
import QtQuick.Controls
import LanChatGui

Dialog {
    id: root
    modal: true
    padding: Theme.spacingL

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Qt.rgba(0.12, 0.14, 0.17, 0.96)
        border.color: Theme.border
        border.width: 1
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 100
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.glassHighlight }
                GradientStop { position: 1.0; color: "transparent" }
            }
            opacity: 0.36
        }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animationNormal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.animationNormal; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animationFast; easing.type: Easing.InCubic }
            NumberAnimation { property: "scale"; from: 1; to: 0.98; duration: Theme.animationFast; easing.type: Easing.InCubic }
        }
    }
}
