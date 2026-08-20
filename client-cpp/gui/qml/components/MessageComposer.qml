import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

RowLayout {
    id: root
    property alias text: input.text
    signal sendRequested()
    height: 58
    spacing: 10

    TextField {
        id: input
        Layout.fillWidth: true
        placeholderText: "输入消息..."
        color: Theme.primaryText
        placeholderTextColor: Theme.secondaryText
        selectByMouse: true
        background: Rectangle {
            radius: 12
            color: Qt.rgba(0.10, 0.14, 0.20, 0.92)
            border.color: input.activeFocus ? Theme.accent : Theme.borderSoft
            border.width: input.activeFocus ? 2 : 1
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                radius: 1
                color: input.activeFocus ? Theme.glassHighlight : "transparent"
            }
            Behavior on border.color { ColorAnimation { duration: 160 } }
        }
        onAccepted: root.sendRequested()
    }
    GlassButton {
        accent: true
        compact: true
        text: "发送"
        enabled: input.text.trim().length > 0
        onClicked: root.sendRequested()
    }
}
