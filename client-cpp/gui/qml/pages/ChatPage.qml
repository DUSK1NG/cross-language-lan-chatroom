import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Item {
    id: root
    property string modeName: "Mock Mode"
    property string activeRoom: "lobby"
    property string activeDirectMessage: ""
    property string headerTitle: "# lobby"

    ListModel {
        id: roomModel
        ListElement { roomName: "lobby"; memberCount: 3; unreadCount: 0 }
        ListElement { roomName: "study"; memberCount: 5; unreadCount: 2 }
        ListElement { roomName: "gaming"; memberCount: 4; unreadCount: 0 }
    }

    ListModel {
        id: directMessageModel
        ListElement { displayName: "Bob"; userCode: "B002"; unreadCount: 2 }
        ListElement { displayName: "Chen"; userCode: "C003"; unreadCount: 0 }
    }

    ListModel {
        id: messageModel
        ListElement { sender: "Alice"; userCode: "A001"; time: "18:24"; content: "今天的学习资料整理好了吗？"; selfMessage: false; systemMessage: false }
        ListElement { sender: "Alice"; userCode: "A001"; time: "18:24"; content: "我还在整理最后一部分。"; selfMessage: false; systemMessage: false }
        ListElement { sender: "Bob"; userCode: "B002"; time: "18:25"; content: "我已经完成了，可以发到这里。"; selfMessage: false; systemMessage: false }
        ListElement { sender: "Mock User"; userCode: "A001"; time: "18:26"; content: "好的，谢谢！"; selfMessage: true; systemMessage: false }
    }

    ListModel {
        id: memberModel
        ListElement { displayName: "Alice"; userCode: "A001"; online: true; admin: true }
        ListElement { displayName: "Bob"; userCode: "B002"; online: true; admin: false }
        ListElement { displayName: "Chen"; userCode: "C003"; online: true; admin: false }
    }

    function selectRoom(roomName) {
        activeRoom = roomName
        activeDirectMessage = ""
        headerTitle = "# " + roomName
    }

    function selectDirectMessage(name, code) {
        activeDirectMessage = code
        headerTitle = "@ " + name
    }

    function appendMockMessage() {
        if (composer.text.trim().length === 0) return
        messageModel.append({
            sender: "Mock User",
            userCode: "A001",
            time: "18:30",
            content: composer.text.trim(),
            selfMessage: true,
            systemMessage: false
        })
        composer.text = ""
        messageList.positionViewAtEnd()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            color: Theme.surface

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                Label {
                    text: "LAN CHAT"
                    color: Theme.primaryText
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Label { text: "房间"; color: Theme.secondaryText; font.pixelSize: 12 }

                Repeater {
                    model: roomModel
                    delegate: RoomItem {
                        Layout.fillWidth: true
                        roomName: model.roomName
                        memberCount: model.memberCount
                        unreadCount: model.unreadCount
                        selected: root.activeRoom === model.roomName && root.activeDirectMessage === ""
                        onItemSelected: root.selectRoom(roomName)
                    }
                }

                Label { text: "私聊"; color: Theme.secondaryText; font.pixelSize: 12; Layout.topMargin: 8 }
                Repeater {
                    model: directMessageModel
                    delegate: DirectMessageItem {
                        Layout.fillWidth: true
                        displayName: model.displayName
                        userCode: model.userCode
                        unreadCount: model.unreadCount
                        selected: root.activeDirectMessage === model.userCode
                        onItemSelected: root.selectDirectMessage(displayName, userCode)
                    }
                }

                Item { Layout.fillHeight: true }
                Label { text: modeName; color: Theme.accent; font.pixelSize: 12 }
                Label { text: "Mock User#A001"; color: Theme.primaryText; font.pixelSize: 13 }
                Label { text: "在线 · Mock 数据"; color: Theme.success; font.pixelSize: 11 }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                ChatHeader {
                    Layout.fillWidth: true
                    title: root.headerTitle
                    subtitle: root.activeDirectMessage === "" ? "学习交流群" : "私聊 · Mock 数据"
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                ListView {
                    id: messageList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8
                    clip: true
                    model: messageModel
                    delegate: MessageDelegate {
                        width: messageList.width
                        sender: model.sender
                        userCode: model.userCode
                        time: model.time
                        content: model.content
                        selfMessage: model.selfMessage
                        systemMessage: model.systemMessage
                    }
                }

                MessageComposer {
                    id: composer
                    Layout.fillWidth: true
                    onSendRequested: root.appendMockMessage()
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 230
            Layout.fillHeight: true
            color: Theme.surface

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                Label {
                    text: "在线成员 · " + memberModel.count
                    color: Theme.primaryText
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: memberModel
                    delegate: MemberItem {
                        Layout.fillWidth: true
                        displayName: model.displayName
                        userCode: model.userCode
                        online: model.online
                        admin: model.admin
                    }
                }
            }
        }
    }
}
