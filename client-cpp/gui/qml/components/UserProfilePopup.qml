import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Popup {
    id: root
    property string displayName: ""
    property string userCode: ""
    property bool admin: false
    property bool canAdmin: false
    property bool selfUser: false
    property string pendingAction: ""
    signal privateRequested(string displayName, string userCode)
    width: 260
    height: canAdmin && !admin && !selfUser ? 330 : 280
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    padding: 18

    function confirmAdminAction(action) {
        pendingAction = action
        adminConfirmDialog.open()
    }

    background: Rectangle {
        radius: 12
        color: Theme.surfaceRaised
        border.color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Avatar { displayName: root.displayName; size: 56; Layout.alignment: Qt.AlignHCenter }
        Label { text: root.displayName; color: Theme.primaryText; font.pixelSize: 20; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
        Label { text: "#" + root.userCode; color: Theme.secondaryText; font.pixelSize: 13 }
        Label { text: root.admin ? "ADMIN" : "普通成员"; color: root.admin ? Theme.accent : Theme.secondaryText; font.pixelSize: 11 }
        Label { text: "在线"; color: Theme.success; font.pixelSize: 12 }
        Item { Layout.fillHeight: true }
        Label { text: root.selfUser ? "这是我的主页" : "用户主页"; color: Theme.secondaryText; font.pixelSize: Theme.fontCaption; Layout.alignment: Qt.AlignHCenter }
        AppButton {
            Layout.fillWidth: true
            text: "私聊"
            enabled: root.userCode.length > 0 && !root.selfUser
            onClicked: {
                root.privateRequested(root.displayName, root.userCode)
                root.close()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.canAdmin && !root.admin && !root.selfUser
            AppButton {
                compact: true
                Layout.fillWidth: true
                text: "禁言/解禁"
                onClicked: root.confirmAdminAction("mute")
            }
            AppButton {
                variant: "danger"
                compact: true
                danger: true
                Layout.fillWidth: true
                text: "踢出"
                onClicked: root.confirmAdminAction("kick")
            }
        }
        AppButton { Layout.fillWidth: true; text: "关闭"; onClicked: root.close() }
    }

    AppDialog {
        id: adminConfirmDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 360
        title: root.pendingAction === "kick" ? "确认踢出成员" : "确认切换禁言"
        modal: true

        contentItem: Label {
            width: 320
            wrapMode: Text.WordWrap
            color: Theme.primaryText
            text: root.pendingAction === "kick"
                  ? "确定将 " + root.displayName + "#" + root.userCode + " 踢出聊天室吗？"
                  : "确定切换 " + root.displayName + "#" + root.userCode + " 的禁言状态吗？"
        }

        footer: RowLayout {
            width: adminConfirmDialog.width - adminConfirmDialog.leftPadding - adminConfirmDialog.rightPadding
            Item { Layout.fillWidth: true }
            AppButton { compact: true; text: "取消"; onClicked: adminConfirmDialog.close() }
            AppButton {
                compact: true
                accent: root.pendingAction !== "kick"
                danger: root.pendingAction === "kick"
                variant: root.pendingAction === "kick" ? "danger" : "primary"
                text: "确认"
                onClicked: {
                    chatController.sendAdminAction(root.pendingAction, root.userCode)
                    adminConfirmDialog.close()
                    root.close()
                }
            }
        }

        onClosed: root.pendingAction = ""
    }
}
