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
            color: Theme.card
            border.color: input.activeFocus ? Theme.accent : Theme.borderSoft
            border.width: input.activeFocus ? 2 : 1
            Behavior on border.color { ColorAnimation { duration: 160 } }
        }
        onAccepted: root.sendRequested()
    }
    Button {
        text: "发送"
        enabled: input.text.trim().length > 0
        background: Rectangle {
            radius: 10
            color: !enabled ? Theme.surfaceRaised : pressed ? Theme.accentPressed : hovered ? Theme.accent : Theme.surfaceRaised
            Behavior on color { ColorAnimation { duration: 160 } }
        }
        contentItem: Text {
            text: parent.text
            color: parent.enabled ? Theme.primaryText : Theme.secondaryText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.weight: Font.DemiBold
        }
        scale: pressed ? 0.97 : 1.0
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        onClicked: root.sendRequested()
    }
}
