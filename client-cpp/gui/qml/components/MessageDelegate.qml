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
    height: systemMessage ? 34 : messageColumn.implicitHeight + 10

    Label {
        anchors.centerIn: parent
        visible: root.systemMessage
        text: root.content
        color: Theme.secondaryText
        font.pixelSize: 11
        font.italic: true
    }

    Column {
        id: messageColumn
        visible: !root.systemMessage
        anchors.left: root.selfMessage ? undefined : parent.left
        anchors.right: root.selfMessage ? parent.right : undefined
        width: Math.min(parent.width * 0.82, 720)
        spacing: 5

        Row {
            anchors.right: root.selfMessage ? parent.right : undefined
            spacing: 7
            Label { text: root.sender; color: root.selfMessage ? Theme.accent : Theme.primaryText; font.pixelSize: 13; font.weight: Font.DemiBold }
            Label { text: "#" + root.userCode + "  " + root.time; color: Theme.secondaryText; font.pixelSize: 11 }
        }
        Rectangle {
            anchors.right: root.selfMessage ? parent.right : undefined
            width: Math.min(messageText.implicitWidth + 28, messageColumn.width)
            height: messageText.implicitHeight + 20
            radius: 11
            color: root.selfMessage ? Theme.selfBubble : Theme.otherBubble
            border.color: root.selfMessage ? Theme.accent : Theme.border
            border.width: root.selfMessage ? 0 : 1
            opacity: root.hovered ? 1.0 : 0.97
            Behavior on opacity { NumberAnimation { duration: 140 } }
            Text {
                id: messageText
                anchors.centerIn: parent
                width: Math.min(implicitWidth, messageColumn.width - 28)
                text: root.content
                wrapMode: Text.Wrap
                horizontalAlignment: root.selfMessage ? Text.AlignRight : Text.AlignLeft
                color: Theme.primaryText
                font.pixelSize: 13
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
