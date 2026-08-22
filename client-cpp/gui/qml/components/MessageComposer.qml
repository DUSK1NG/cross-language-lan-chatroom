import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

RowLayout {
    id: root
    property alias text: input.text
    property int minimumInputHeight: 42
    property int maximumInputHeight: 116
    signal sendRequested()
    height: Math.max(58, input.implicitHeight)
    spacing: Theme.spacingS

    TextArea {
        id: input
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(root.maximumInputHeight,
                                         Math.max(root.minimumInputHeight, contentHeight + topPadding + bottomPadding))
        Layout.minimumHeight: root.minimumInputHeight
        Layout.maximumHeight: root.maximumInputHeight
        implicitHeight: Math.min(root.maximumInputHeight,
                                 Math.max(root.minimumInputHeight, contentHeight + topPadding + bottomPadding))
        placeholderText: "输入消息...（Enter 发送，Shift+Enter 换行）"
        color: Theme.primaryText
        placeholderTextColor: Theme.secondaryText
        selectByMouse: true
        wrapMode: TextArea.Wrap
        persistentSelection: true
        font.pixelSize: Theme.fontBody
        leftPadding: Theme.spacingM
        rightPadding: Theme.spacingM
        topPadding: Theme.spacingS
        bottomPadding: Theme.spacingS
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
            Behavior on border.color { enabled: typeof performanceProfile === "undefined" || performanceProfile.animationsEnabled; ColorAnimation { duration: Theme.animationNormal } }
        }
        Keys.onPressed: function(event) {
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) &&
                !(event.modifiers & Qt.ShiftModifier)) {
                event.accepted = true
                root.sendRequested()
            }
        }
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

    Label {
        Layout.alignment: Qt.AlignBottom
        Layout.bottomMargin: Theme.spacingS
        text: input.length > 0 ? input.length : ""
        color: input.length > 3600 ? Theme.danger : Theme.secondaryText
        font.pixelSize: Theme.fontCaption
        visible: input.length > 0
    }

}
