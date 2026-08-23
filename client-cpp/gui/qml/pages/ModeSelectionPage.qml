import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Item {
    id: root
    signal modeSelected(string mode)
    property bool localHostAvailable: typeof hostAvailable !== "undefined" && hostAvailable
    property string localHostUnavailableMessage: typeof hostUnavailableMessage !== "undefined"
                                               ? hostUnavailableMessage : "此安装包不包含本地服务端"

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 920)
        spacing: Theme.spacingM

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "选择聊天方式"
            color: Theme.primaryText
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "安全、稳定的 Go + Qt 局域网聊天"
            color: Theme.secondaryText
            font.pixelSize: Theme.fontBody
        }

        RowLayout {
            Layout.topMargin: Theme.spacingL
            Layout.fillWidth: true
            spacing: Theme.spacingM

            ModeCard {
                title: "远程服务器"
                subtitle: "连接已经部署的 Go Server"
                iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/server.svg"
                onClicked: root.modeSelected("Remote Server")
            }

            ModeCard {
                title: "创建本地聊天室"
                subtitle: root.localHostAvailable ? "当前电脑作为 Host" : root.localHostUnavailableMessage
                iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/network.svg"
                available: root.localHostAvailable
                onClicked: root.modeSelected("Local Host")
            }

            ModeCard {
                title: "加入局域网聊天室"
                subtitle: "作为 Guest 加入房主"
                iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/users.svg"
                onClicked: root.modeSelected("Guest")
            }
        }
    }

    component ModeCard: Rectangle {
        id: card
        property string title: ""
        property string subtitle: ""
        property string iconSource: ""
        property bool available: true
        signal clicked()
        Layout.fillWidth: true
        Layout.preferredHeight: 196
        radius: Theme.radiusLarge
        color: available && mouse.containsMouse ? Theme.surfaceRaised : Theme.surface
        border.color: available && mouse.containsMouse ? Theme.accent : Theme.border
        border.width: 1
        opacity: available ? 1.0 : 0.52
        scale: available && mouse.pressed ? 0.985 : available && mouse.containsMouse ? 1.01 : 1.0
        Behavior on color { ColorAnimation { duration: Theme.animationNormal } }
        Behavior on border.color { ColorAnimation { duration: Theme.animationNormal } }
        Behavior on scale { NumberAnimation { duration: Theme.animationFast; easing.type: Easing.OutCubic } }

        Column {
            anchors.centerIn: parent
            width: parent.width - 32
            spacing: Theme.spacingM

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: card.iconSource
                sourceSize.width: 42
                sourceSize.height: 42
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: card.title
                color: Theme.primaryText
                font.pixelSize: Theme.fontBodyLarge
                font.weight: Font.DemiBold
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: card.subtitle
                color: Theme.secondaryText
                font.pixelSize: Theme.fontCaption
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: card.available
            onClicked: card.clicked()
        }
    }
}
