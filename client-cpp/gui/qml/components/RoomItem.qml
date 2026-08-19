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
    height: 34
    radius: 6
    color: selected ? Theme.surfaceRaised : mouse.containsMouse ? "#252a34" : "transparent"

    Label {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: "# " + root.roomName
        color: root.selected ? Theme.primaryText : Theme.secondaryText
        font.pixelSize: 13
    }
    Label {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: root.unreadCount > 0 ? root.unreadCount : root.memberCount
        color: root.unreadCount > 0 ? Theme.accent : Theme.secondaryText
        font.pixelSize: 11
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.itemSelected()
    }
}
