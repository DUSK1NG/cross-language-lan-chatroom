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
    property var roomModel: chatController.filteredRoomModel
    property var directMessageModel: chatController.filteredDirectMessageModel
    property var messageModel: chatController.activeMessageModel
    property var memberModel: chatController.memberModel
    property string sidebarQuery: ""
    property bool compactLayout: width < 1200
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
            return
        }
        composer.text = ""
        messageList.followTail = true
        messageList.positionViewAtEnd()
    }

    function quoteMessage(sender, content) {
        composer.text = "> " + sender + "：" + content + "\n"
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
                visible: performanceProfile.gradientsEnabled
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                Label { text: "LAN CHAT"; color: Theme.primaryText; font.pixelSize: Theme.fontBodyLarge; font.weight: Font.DemiBold }
                AppTextField {
                    Layout.fillWidth: true
                    iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/search.svg"
                    placeholderText: "搜索频道或私聊"
                    onTextChanged: {
                        root.sidebarQuery = text.trim().toLowerCase()
                        chatController.setSidebarQuery(root.sidebarQuery)
                    }
                }
                AppButton { Layout.fillWidth: true; variant: "primary"; iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/plus.svg"; text: "新建频道"; onClicked: createRoomDialog.open() }
                Label { text: "CHANNELS"; color: Theme.secondaryText; font.pixelSize: Theme.fontCaption; font.weight: Font.DemiBold }

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

                Label { text: "DIRECT MESSAGES"; color: Theme.secondaryText; font.pixelSize: Theme.fontCaption; font.weight: Font.DemiBold; Layout.topMargin: Theme.spacingS }
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
                visible: performanceProfile.gradientsEnabled
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 14

                ChatHeader {
                    Layout.fillWidth: true
                    onSettingsRequested: {
                        if (root.activeDirectMessage === "" && chatController.activeRoomCanManage) roomSettingsDialog.open()
                    }
                    onMembersRequested: memberPopup.open()
                    showMembersButton: root.compactLayout
                    title: root.headerTitle
                    subtitle: root.activeDirectMessage === ""
                              ? (chatController.connected ? "学习交流 · Go TLS Server" : "学习交流 · 未连接")
                              : (chatController.connected ? "私聊 · Go TLS Server" : "私聊 · 未连接")
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSoft }

                ListView {
                    id: messageList
                    property bool followTail: true
                    property real prependContentHeight: -1
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    cacheBuffer: 640
                    model: root.messageModel
                    onModelChanged: {
                        followTail = true
                        Qt.callLater(positionViewAtEnd)
                    }
                    onCountChanged: {
                        if (followTail) Qt.callLater(positionViewAtEnd)
                    }
                    onContentHeightChanged: {
                        if (followTail) Qt.callLater(positionViewAtEnd)
                    }
                    onMovementEnded: followTail = atYEnd
                    onAtYBeginningChanged: {
                        if (atYBeginning && count > 0) chatController.loadMoreHistory()
                    }
                    delegate: MessageDelegate {
                        width: messageList.width
                        messageId: model.messageId
                        displayName: model.displayName
                        userCode: model.userCode
                        time: model.time
                        content: model.content
                        selfMessage: model.selfMessage
                        systemMessage: model.systemMessage
                        canRecall: chatController.admin && messageId.length > 0
                        onCopyRequested: chatController.copyText(content)
                        onQuoteRequested: function(senderName, messageContent) { root.quoteMessage(senderName, messageContent) }
                        onLocalDeleteRequested: chatController.removeLocalMessage(messageId)
                        onRecallRequested: chatController.recallMessage(messageId)
                        onProfileRequested: root.showProfile(displayName, userCode, false)
                    }
                    ScrollBar.vertical: AppScrollBar { }

                    Connections {
                        target: root.messageModel
                        function onRowsAboutToBeInserted(parent, first, last) {
                            if (first === 0 && !messageList.followTail) {
                                messageList.prependContentHeight = messageList.contentHeight
                            }
                        }
                        function onRowsInserted(parent, first, last) {
                            if (first === 0 && messageList.prependContentHeight >= 0 && !messageList.followTail) {
                                const previousHeight = messageList.prependContentHeight
                                messageList.prependContentHeight = -1
                                Qt.callLater(function() {
                                    messageList.contentY += messageList.contentHeight - previousHeight
                                })
                            }
                        }
                    }
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
            visible: !root.compactLayout
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
                visible: performanceProfile.gradientsEnabled
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
                    AppButton {
                        compact: true
                        iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/refresh.svg"
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
                    ScrollBar.vertical: AppScrollBar { }
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

    AppToast {
        id: phaseToast
    }

    Connections {
        target: chatController
        function onConnectionFailed(reason) { phaseToast.show(reason, "error") }
        function onConnectionLost(reason) { phaseToast.show(reason, "error") }
    }

    AppPopup {
        id: memberPopup
        width: 280
        height: Math.min(root.height - 48, 520)
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        padding: Theme.spacingM

        background: Rectangle {
            radius: Theme.radiusLarge
            color: Qt.rgba(0.12, 0.14, 0.17, 0.97)
            border.color: Theme.border
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingS
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "在线成员 · " + chatController.onlineMemberCount
                    color: Theme.primaryText
                    font.pixelSize: Theme.fontBodyLarge
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                IconButton {
                    iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/close.svg"
                    tooltipText: "关闭"
                    onClicked: memberPopup.close()
                }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: Theme.spacingXS
                model: root.memberModel
                delegate: MemberItem {
                    width: memberPopup.width - Theme.spacingXL
                    displayName: model.displayName
                    userCode: model.userCode
                    online: model.online
                    admin: model.admin
                    onUserSelected: {
                        root.showProfile(displayName, userCode, admin)
                        memberPopup.close()
                    }
                }
                ScrollBar.vertical: AppScrollBar { }
            }
        }
    }

    AppDialog {
        id: createRoomDialog
        title: "新建频道"
        modal: true
        width: 520
        padding: 16
        anchors.centerIn: Overlay.overlay
        footer: RowLayout {
            width: createRoomDialog.width - createRoomDialog.leftPadding - createRoomDialog.rightPadding
            spacing: 8
            AppButton {
                compact: true
                text: "取消"
                onClicked: createRoomDialog.reject()
            }
            AppButton {
                compact: true
                accent: true
                text: "确定"
                enabled: roomNameInput.text.trim().length > 0
                onClicked: createRoomDialog.accept()
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
            AppTextField {
                id: roomNameInput
                Layout.fillWidth: true
                placeholderText: "例如 study_group"
                validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9_]{1,32}$/ }
            }
            CheckBox {
                id: privateRoomCheck
                text: "设为私有频道（仅受邀成员可见和加入）"
                checked: false
            }
        }

        onAccepted: {
            const roomName = roomNameInput.text.trim()
            if (roomName.length > 0) chatController.createRoom(roomName, privateRoomCheck.checked)
            roomNameInput.clear()
            privateRoomCheck.checked = false
        }
        onRejected: {
            roomNameInput.clear()
            privateRoomCheck.checked = false
        }
    }

    AppDialog {
        id: roomSettingsDialog
        title: "频道管理"
        modal: true
        width: 520
        padding: 16
        anchors.centerIn: Overlay.overlay
        ColumnLayout {
            width: roomSettingsDialog.width - roomSettingsDialog.leftPadding - roomSettingsDialog.rightPadding
            spacing: 12
            Label { text: "管理 #" + root.activeRoom; color: Theme.primaryText; font.bold: true }
            AppTextField { id: memberCodeInput; Layout.fillWidth: true; placeholderText: "成员代码，例如 B001" }
            RowLayout {
                Layout.fillWidth: true
                AppButton { text: "邀请成员"; accent: true; enabled: memberCodeInput.text.trim().length > 0; onClicked: { chatController.sendRoomAction("invite", root.activeRoom, memberCodeInput.text); memberCodeInput.clear() } }
                AppButton { text: "移除成员"; enabled: memberCodeInput.text.trim().length > 0; onClicked: { chatController.sendRoomAction("remove_member", root.activeRoom, memberCodeInput.text); memberCodeInput.clear() } }
            }
            AppButton { Layout.fillWidth: true; text: "删除频道"; danger: true; onClicked: { chatController.sendRoomAction("delete", root.activeRoom); roomSettingsDialog.close(); root.selectRoom("lobby") } }
        }
    }
}
