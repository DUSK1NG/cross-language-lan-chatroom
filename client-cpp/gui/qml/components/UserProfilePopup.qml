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
    signal privateRequested(string displayName, string userCode)
    width: 260
    height: canAdmin && !admin && !selfUser ? 330 : 280
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    padding: 18

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
                onClicked: { chatController.sendAdminAction("mute", root.userCode); root.close() }
            }
            AppButton {
                variant: "danger"
                compact: true
                danger: true
                Layout.fillWidth: true
                text: "踢出"
                onClicked: { chatController.sendAdminAction("kick", root.userCode); root.close() }
            }
        }
        AppButton { Layout.fillWidth: true; text: "关闭"; onClicked: root.close() }
    }
}
