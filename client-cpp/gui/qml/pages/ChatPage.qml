import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Item {
    id: root
    property string modeName: "LAN Chat"
    property string activeRoom: "lobby"
    property string activeDirectMessage: ""
    property string headerTitle: "# lobby"
    property var roomModel: chatController.roomModel
    property var directMessageModel: chatController.directMessageModel
    property var messageModel: chatController.activeMessageModel
    property var memberModel: chatController.memberModel
    signal settingsRequested()

    function selectRoom(roomName) {
        activeRoom = roomName
        activeDirectMessage = ""
        headerTitle = "# " + roomName
        chatController.selectRoom(roomName)
    }

    function selectDirectMessage(name, code) {
        activeDirectMessage = code
        headerTitle = "@ " + name
        chatController.selectDirectMessage(code)
    }

    function appendMessage() {
        if (composer.text.trim().length === 0) return
        if (chatController.connected) {
            if (root.activeDirectMessage === "") {
                chatController.sendRoomMessage(composer.text, root.activeRoom)
            } else {
                chatController.sendPrivateMessage(composer.text, root.activeDirectMessage)
            }
        } else {
            chatController.sendMockMessage(composer.text)
        }
        composer.text = ""
        messageList.positionViewAtEnd()
    }

    function showProfile(name, code, isAdmin) {
        profilePopup.displayName = name
        profilePopup.userCode = code
        profilePopup.admin = isAdmin
        profilePopup.canAdmin = chatController.admin
        profilePopup.selfUser = code.toLowerCase() === chatController.localUserCode.toLowerCase()
        profilePopup.open()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 264
            Layout.fillHeight: true
            color: Theme.panel
            border.color: Theme.borderSoft
            border.width: 1

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 150
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.glassHighlight }
                    GradientStop { position: 1.0; color: "transparent" }
                }
                opacity: 0.28
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                Label { text: "LAN CHAT"; color: Theme.primaryText; font.pixelSize: 16; font.weight: Font.DemiBold }
                GlassButton { Layout.fillWidth: true; accent: true; text: "新建频道"; onClicked: createRoomDialog.open() }
                Label { text: "房间"; color: Theme.secondaryText; font.pixelSize: 12 }

                Repeater {
                    model: root.roomModel
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
                    model: root.directMessageModel
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
                Label {
                    text: chatController.connected ? "Go TLS Server" : modeName
                    color: Theme.accent
                    font.pixelSize: 12
                }
                Label {
                    text: chatController.connected
                          ? chatController.localUserName + "#" + chatController.localUserCode
                          : "未连接"
                    color: Theme.primaryText
                    font.pixelSize: 13
                }
                Label {
                    text: chatController.admin ? "在线 · 管理员" : "在线"
                    color: chatController.admin ? Theme.accent : Theme.success
                    font.pixelSize: 11
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Qt.rgba(0.05, 0.07, 0.11, 0.66)
            border.color: Theme.borderSoft
            border.width: 1

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 180
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.glassHighlight }
                    GradientStop { position: 1.0; color: "transparent" }
                }
                opacity: 0.16
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 14

                ChatHeader {
                    Layout.fillWidth: true
                    onSettingsRequested: root.settingsRequested()
                    title: root.headerTitle
                    subtitle: root.activeDirectMessage === ""
                              ? (chatController.connected ? "学习交流 · Go TLS Server" : "学习交流 · 未连接")
                              : (chatController.connected ? "私聊 · Go TLS Server" : "私聊 · 未连接")
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSoft }

                ListView {
                    id: messageList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    cacheBuffer: 640
                    model: root.messageModel
                    delegate: MessageDelegate {
                        width: messageList.width
                        sender: model.sender
                        userCode: model.userCode
                        time: model.time
                        content: model.content
                        selfMessage: model.selfMessage
                        systemMessage: model.systemMessage
                    }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                }

                MessageComposer {
                    id: composer
                    Layout.fillWidth: true
                    onSendRequested: root.appendMessage()
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 248
            Layout.fillHeight: true
            color: Theme.panel
            border.color: Theme.borderSoft
            border.width: 1

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 150
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.glassHighlight }
                    GradientStop { position: 1.0; color: "transparent" }
                }
                opacity: 0.24
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "在线成员 · " + chatController.onlineMemberCount
                        color: Theme.primaryText
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                    }
                    GlassButton {
                        compact: true
                        text: "刷新"
                        onClicked: { chatController.requestUsers(); chatController.requestRooms() }
                    }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 3
                    boundsBehavior: Flickable.StopAtBounds
                    cacheBuffer: 320
                    model: root.memberModel
                    delegate: MemberItem {
                        Layout.fillWidth: true
                        displayName: model.displayName
                        userCode: model.userCode
                        online: model.online
                        admin: model.admin
                        onUserSelected: root.showProfile(displayName, userCode, admin)
                    }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                }
            }
        }
    }

    UserProfilePopup {
        id: profilePopup
        onPrivateRequested: function(displayName, userCode) {
            chatController.openPrivateChat(displayName, userCode)
            root.activeDirectMessage = userCode
            root.headerTitle = "@ " + displayName
        }
    }

    Dialog {
        id: createRoomDialog
        title: "新建频道"
        modal: true
        width: 520
        padding: 16
        anchors.centerIn: Overlay.overlay
        footer: RowLayout {
            width: createRoomDialog.width - createRoomDialog.leftPadding - createRoomDialog.rightPadding
            spacing: 8
            GlassButton {
                compact: true
                text: "取消"
                onClicked: createRoomDialog.reject()
            }
            GlassButton {
                compact: true
                accent: true
                text: "确定"
                enabled: roomNameInput.text.trim().length > 0
                onClicked: createRoomDialog.accept()
            }
        }

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 160; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale"; from: 0.96; to: 1.0; duration: 180; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 110; easing.type: Easing.InCubic }
                NumberAnimation { property: "scale"; from: 1.0; to: 0.98; duration: 110; easing.type: Easing.InCubic }
            }
        }

        ColumnLayout {
            width: createRoomDialog.width - createRoomDialog.leftPadding - createRoomDialog.rightPadding
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: "频道名只能使用字母、数字和下划线"
                color: Theme.secondaryText
                wrapMode: Text.WordWrap
            }
            TextField {
                id: roomNameInput
                Layout.fillWidth: true
                placeholderText: "例如 study_group"
                validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9_]{1,32}$/ }
            }
        }

        onAccepted: {
            const roomName = roomNameInput.text.trim()
            if (roomName.length > 0) root.selectRoom(roomName)
            roomNameInput.clear()
        }
        onRejected: roomNameInput.clear()
    }
}
