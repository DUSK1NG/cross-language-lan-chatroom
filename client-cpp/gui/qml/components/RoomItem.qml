import QtQuick
import QtQuick.Controls
import LanChatGui

Rectangle {
    id: root
    property string roomName: ""
    property int memberCount: 0
    property int unreadCount: 0
    property bool selected: false
    signal itemSelected()
    height: 40
    radius: Theme.radiusMedium
    color: selected ? Theme.accentSoft : mouse.containsMouse ? Theme.surfaceHover : Qt.rgba(1, 1, 1, 0.015)
    border.color: selected ? Theme.glassHighlight : "transparent"
    border.width: selected ? 1 : 0
    scale: mouse.pressed ? 0.985 : 1.0
    Behavior on color { ColorAnimation { duration: 160 } }
    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

    Label {
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingM
        anchors.verticalCenter: parent.verticalCenter
        text: "# " + root.roomName
        color: root.selected ? Theme.primaryText : Theme.secondaryText
        font.pixelSize: Theme.fontBody
    }
    Label {
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingM
        anchors.verticalCenter: parent.verticalCenter
        text: root.unreadCount > 0 ? root.unreadCount : root.memberCount
        color: root.unreadCount > 0 ? Theme.accent : Theme.secondaryText
        font.pixelSize: Theme.fontCaption
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.itemSelected()
    }
}
