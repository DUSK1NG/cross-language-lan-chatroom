import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Popup {
    id: root
    property string displayName: ""
    property string userCode: ""
    property bool admin: false
    width: 260
    height: 230
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
        Label { text: "在线 · Mock 数据"; color: Theme.success; font.pixelSize: 12 }
        Item { Layout.fillHeight: true }
        Button { Layout.fillWidth: true; text: "发送私聊（Mock）"; onClicked: root.close() }
        Button { Layout.fillWidth: true; text: "关闭"; onClicked: root.close() }
    }
}
