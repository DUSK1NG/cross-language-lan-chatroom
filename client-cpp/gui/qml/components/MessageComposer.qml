import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

RowLayout {
    id: root
    property alias text: input.text
    signal sendRequested()
    height: 58
    spacing: Theme.spacingS

    AppTextField {
        id: input
        Layout.fillWidth: true
        placeholderText: "输入消息..."
        color: Theme.primaryText
        placeholderTextColor: Theme.secondaryText
        selectByMouse: true
        background: Rectangle {
            radius: Theme.radiusMedium
            color: input.activeFocus ? Qt.rgba(0.96, 0.98, 1.0, 0.14)
                                    : Qt.rgba(0.90, 0.93, 0.97, 0.08)
            border.color: input.activeFocus ? Theme.accent : Theme.borderSoft
            border.width: input.activeFocus ? 2 : 1
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: input.activeFocus ? Theme.glassHighlight : "transparent"
            }
            Behavior on border.color { ColorAnimation { duration: Theme.animationNormal } }
        }
        onAccepted: root.sendRequested()
    }
    AppButton {
        variant: "primary"
        accent: true
        compact: true
        iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/send.svg"
        text: "发送"
        enabled: input.text.trim().length > 0
        onClicked: root.sendRequested()
    }

    IconButton {
        iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/emoji.svg"
        tooltipText: "表情"
        onClicked: emojiPicker.open()
    }

    EmojiPicker {
        id: emojiPicker
        x: Math.max(0, root.width - width)
        y: -height - Theme.spacingS
        onEmojiSelected: function(emoji) {
            input.insert(input.cursorPosition, emoji)
            input.forceActiveFocus()
        }
    }
}
