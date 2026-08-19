import QtQuick
import QtQuick.Controls
import LanChatGui

Rectangle {
    id: root
    property string displayName: ""
    property string userCode: ""
    property int unreadCount: 0
    property bool selected: false
    signal itemSelected()
    height: 38
    radius: 6
    color: selected ? Theme.surfaceRaised : mouse.containsMouse ? "#252a34" : "transparent"

    Label {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: "●  " + root.displayName
        color: Theme.primaryText
        font.pixelSize: 13
    }
    Label {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: root.unreadCount > 0 ? root.unreadCount : ""
        color: Theme.accent
        font.pixelSize: 11
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.itemSelected()
    }
}
