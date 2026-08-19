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
    height: 42
    radius: 8
    color: selected ? Theme.accentSoft : mouse.containsMouse ? Theme.surfaceHover : "transparent"
    scale: mouse.pressed ? 0.985 : 1.0
    Behavior on color { ColorAnimation { duration: 160 } }
    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

    Label {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: "●  " + root.displayName
        color: Theme.primaryText
        font.pixelSize: 13
    }
    Label {
        anchors.right: parent.right
        anchors.rightMargin: 12
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
