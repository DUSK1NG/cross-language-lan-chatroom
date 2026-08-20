import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Item {
    id: root
    signal backRequested()
    property string modeName: "Remote Server"

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 560)
        spacing: 14

        Label {
            text: root.modeName === "Guest" ? "加入局域网聊天室" : "连接 Go TLS Server"
            color: Theme.primaryText
            font.pixelSize: 26
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: chatController.statusText
            color: chatController.connected ? Theme.success : Theme.secondaryText
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: root.modeName === "Guest"
                  ? "同一台电脑测试填 127.0.0.1；另一台电脑请填写 Alice 电脑的局域网 IPv4，例如 192.168.0.3"
                  : "请输入 Go Server 所在电脑的 IPv4 地址"
            color: Theme.accent
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        GridLayout {
            columns: 2
            Layout.fillWidth: true
            columnSpacing: 12
            rowSpacing: 10

            Label { text: "服务器 IP"; color: Theme.primaryText }
            TextField {
                id: serverIp
                Layout.fillWidth: true
                text: "127.0.0.1"
                placeholderText: "Alice 电脑 IPv4，例如 192.168.0.3"
                selectByMouse: true
            }

            Label { text: "端口"; color: Theme.primaryText }
            TextField {
                id: serverPort
                Layout.fillWidth: true
                text: "8888"
                validator: IntValidator { bottom: 1; top: 65535 }
                selectByMouse: true
            }

            Label { text: "用户名"; color: Theme.primaryText }
            TextField { id: username; Layout.fillWidth: true; text: root.modeName === "Guest" ? "Bob" : "Alice"; selectByMouse: true }

            Label { text: "用户代码"; color: Theme.primaryText }
            TextField { id: userCode; Layout.fillWidth: true; text: root.modeName === "Guest" ? "B001" : "A001"; selectByMouse: true }

            Label { text: "密码"; color: Theme.primaryText }
            TextField {
                id: password
                Layout.fillWidth: true
                echoMode: TextInput.Password
                selectByMouse: true
            }

            Label { text: "CA 文件"; color: Theme.primaryText }
            TextField {
                id: caFile
                Layout.fillWidth: true
                placeholderText: "留空则使用系统证书"
                selectByMouse: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            GlassButton { text: "返回"; onClicked: root.backRequested() }
            Item { Layout.fillWidth: true }
            GlassButton {
                accent: true
                text: "连接"
                enabled: serverIp.text.length > 0 && serverPort.text.length > 0 &&
                         username.text.length > 0 && userCode.text.length > 0
                onClicked: {
                    chatController.connectToServer(serverIp.text,
                                                   Number(serverPort.text),
                                                   username.text,
                                                   userCode.text,
                                                   password.text,
                                                   caFile.text,
                                                   false)
                }
            }
        }
    }
}
