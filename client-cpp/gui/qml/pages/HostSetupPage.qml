import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Item {
    id: root
    signal backRequested()

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 650)
        spacing: 12

        Label { text: "创建本地聊天室"; color: Theme.primaryText; font.pixelSize: 26; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
        Label { text: chatController.statusText; color: chatController.connected ? Theme.success : Theme.secondaryText; Layout.alignment: Qt.AlignHCenter }

        GridLayout {
            columns: 2; Layout.fillWidth: true; columnSpacing: 12; rowSpacing: 10
            Label { text: "用户名"; color: Theme.primaryText }
            AppTextField { id: username; Layout.fillWidth: true; text: "Alice" }
            Label { text: "用户代码"; color: Theme.primaryText }
            AppTextField { id: userCode; Layout.fillWidth: true; text: "A001" }
            Label { text: "Go Server"; color: Theme.primaryText }
            AppTextField { id: serverExe; Layout.fillWidth: true; text: hostServerExe }
            Label { text: "证书"; color: Theme.primaryText }
            AppTextField { id: certFile; Layout.fillWidth: true; text: hostCertFile }
            Label { text: "私钥"; color: Theme.primaryText }
            AppTextField { id: keyFile; Layout.fillWidth: true; text: hostKeyFile }
            Label { text: "数据库"; color: Theme.primaryText }
            AppTextField { id: dbFile; Layout.fillWidth: true; text: hostDbFile }
        }

        RowLayout {
            Layout.fillWidth: true
            AppButton { text: "返回"; onClicked: root.backRequested() }
            Item { Layout.fillWidth: true }
            AppButton {
                variant: "primary"
                accent: true
                text: "启动本地聊天室"
                enabled: !chatController.connected && username.text.length > 0 && userCode.text.length > 0
                onClicked: chatController.connectToLocalHost(serverExe.text, certFile.text, keyFile.text, dbFile.text, username.text, userCode.text)
            }
        }
    }
}
