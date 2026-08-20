import QtQuick
import QtQuick.Controls
import LanChatGui

ScrollBar {
    id: root
    width: 8
    policy: ScrollBar.AsNeeded
    contentItem: Rectangle {
        implicitWidth: 6
        radius: Theme.radiusRound
        color: root.pressed ? Theme.accent : root.hovered ? Theme.border : Theme.borderSoft
        opacity: root.active || root.hovered ? 0.90 : 0.45
        Behavior on opacity { NumberAnimation { duration: Theme.animationFast } }
    }
}
