import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Item {
    id: root

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 920)
        spacing: 12

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "选择聊天方式"
            color: Theme.primaryText
            font.pixelSize: 28
            font.weight: Font.DemiBold
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "Phase 1 · UI 骨架 · 当前使用 Mock 数据"
            color: Theme.secondaryText
            font.pixelSize: 13
        }

        RowLayout {
            Layout.topMargin: 20
            Layout.fillWidth: true
            spacing: 14

            ModeCard {
                title: "远程服务器"
                subtitle: "连接已经部署的 Go Server"
                symbol: "▣"
                onClicked: root.parent.parent.openChat("Remote Server")
            }

            ModeCard {
                title: "创建本地聊天室"
                subtitle: "当前电脑作为 Host"
                symbol: "⌂"
                onClicked: root.parent.parent.openChat("Local Host")
            }

            ModeCard {
                title: "加入局域网聊天室"
                subtitle: "作为 Guest 加入房主"
                symbol: "⇢"
                onClicked: root.parent.parent.openChat("Guest")
            }
        }
    }

    component ModeCard: Rectangle {
        id: card
        property string title: ""
        property string subtitle: ""
        property string symbol: ""
        signal clicked()
        Layout.fillWidth: true
        Layout.preferredHeight: 190
        radius: 12
        color: mouse.containsMouse ? Theme.surfaceRaised : Theme.surface
        border.color: mouse.containsMouse ? Theme.accent : Theme.border
        border.width: 1

        Column {
            anchors.centerIn: parent
            width: parent.width - 32
            spacing: 10

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: card.symbol
                color: Theme.accent
                font.pixelSize: 38
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: card.title
                color: Theme.primaryText
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: card.subtitle
                color: Theme.secondaryText
                font.pixelSize: 12
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: card.clicked()
        }
    }
}
