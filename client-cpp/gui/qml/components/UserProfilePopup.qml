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
    height: canAdmin && !admin && !selfUser ? 280 : 230
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
        Label { text: root.displayName; color: Theme.primaryText; font.pixelSize: 20; font.weight: Font.DemiBold }
        Label { text: "#" + root.userCode; color: Theme.secondaryText; font.pixelSize: 13 }
        Label { text: root.admin ? "ADMIN" : "普通成员"; color: root.admin ? Theme.accent : Theme.secondaryText; font.pixelSize: 11 }
        Label { text: "在线"; color: Theme.success; font.pixelSize: 12 }
        Item { Layout.fillHeight: true }
        GlassButton {
            Layout.fillWidth: true
            text: "主页"
            onClicked: root.close()
        }
        GlassButton {
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
            GlassButton {
                compact: true
                Layout.fillWidth: true
                text: "禁言/解禁"
                onClicked: { chatController.sendAdminAction("mute", root.userCode); root.close() }
            }
            GlassButton {
                compact: true
                danger: true
                Layout.fillWidth: true
                text: "踢出"
                onClicked: { chatController.sendAdminAction("kick", root.userCode); root.close() }
            }
        }
        GlassButton { Layout.fillWidth: true; text: "关闭"; onClicked: root.close() }
    }
}
