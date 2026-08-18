import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

RowLayout {
    id: root
    property alias text: input.text
    signal sendRequested()
    height: 54
    spacing: 8

    TextField {
        id: input
        Layout.fillWidth: true
        placeholderText: "输入消息..."
        color: Theme.primaryText
        placeholderTextColor: Theme.secondaryText
        selectByMouse: true
        background: Rectangle {
            radius: 10
            color: Theme.surface
            border.color: input.activeFocus ? Theme.accent : Theme.border
        }
        onAccepted: root.sendRequested()
    }
    Button {
        text: "发送"
        enabled: input.text.trim().length > 0
        onClicked: root.sendRequested()
    }
}
