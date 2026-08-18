import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Item {
    id: root
    property string sender: ""
    property string userCode: ""
    property string time: ""
    property string content: ""
    property bool selfMessage: false
    property bool systemMessage: false
    property bool hovered: hoverArea.containsMouse
    height: systemMessage ? 28 : messageColumn.implicitHeight + 4

    Label {
        anchors.centerIn: parent
        visible: root.systemMessage
        text: root.content
        color: Theme.secondaryText
        font.pixelSize: 12
    }

    Column {
        id: messageColumn
        visible: !root.systemMessage
        anchors.left: root.selfMessage ? undefined : parent.left
        anchors.right: root.selfMessage ? parent.right : undefined
        width: Math.min(parent.width * 0.72, 620)
        spacing: 3

        Row {
            anchors.right: root.selfMessage ? parent.right : undefined
            spacing: 8
            Label { text: root.sender; color: Theme.primaryText; font.pixelSize: 13; font.weight: Font.DemiBold }
            Label { text: "#" + root.userCode + "  " + root.time; color: Theme.secondaryText; font.pixelSize: 11 }
        }
        Rectangle {
            width: messageText.implicitWidth + 24
            height: messageText.implicitHeight + 18
            radius: 8
            color: root.selfMessage ? Theme.accentPressed : Theme.surface
            border.color: Theme.border
            Text {
                id: messageText
                anchors.centerIn: parent
                width: Math.min(implicitWidth, messageColumn.width - 24)
                text: root.content
                wrapMode: Text.Wrap
                color: Theme.primaryText
                font.pixelSize: 14
            }
        }
        Row {
            visible: root.hovered
            anchors.right: root.selfMessage ? parent.right : undefined
            spacing: 6
            Label { text: "复制"; color: Theme.accent; font.pixelSize: 10 }
            Label { text: "更多"; color: Theme.secondaryText; font.pixelSize: 10 }
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }
}
