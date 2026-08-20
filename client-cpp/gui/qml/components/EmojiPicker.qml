import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Popup {
    id: picker
    width: 380
    height: 326
    padding: Theme.spacingM
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    signal emojiSelected(string emoji)

    property int categoryIndex: 0
    property var categoryNames: ["常用", "表情", "手势", "动物", "食物", "活动"]
    property var categoryEmojis: [
        ["😀", "😂", "🤣", "😊", "😍", "😘", "😎", "🤔", "😮", "😢", "😡", "👍", "👎", "👏", "🙏", "❤️", "🎉", "🔥", "✨", "✅"],
        ["😀", "😃", "😄", "😁", "😆", "😅", "🤣", "😂", "🙂", "🙃", "😉", "😊", "😇", "🥰", "😍", "🤩", "😘", "😗", "😚", "😋", "😛", "😜", "🤪", "🤗", "🤭", "🫢", "🤫", "🤔", "🫡", "🤐", "🤨", "😐", "😑", "😶", "🫥", "😏", "😒", "🙄", "😬", "😮", "🤐", "😴", "🤤", "😷", "🤒", "🤕", "🤢", "🤮", "🥵", "🥶"],
        ["👍", "👎", "👌", "✌️", "🤞", "🤟", "🤘", "🤙", "👈", "👉", "👆", "👇", "☝️", "✋", "🤚", "🖐️", "🖖", "👏", "🙌", "👐", "🤲", "🙏", "💪", "🫶", "👋", "🤝", "✍️", "💅", "👀", "🧠"],
        ["🐶", "🐱", "🐭", "🐹", "🐰", "🦊", "🐻", "🐼", "🐨", "🐯", "🦁", "🐮", "🐷", "🐸", "🐵", "🙈", "🙉", "🙊", "🐔", "🐧", "🐦", "🦄", "🐝", "🦋", "🐢", "🐍", "🦖", "🐙", "🦀", "🐳"],
        ["🍎", "🍐", "🍊", "🍋", "🍌", "🍉", "🍇", "🍓", "🫐", "🍒", "🍑", "🥭", "🍍", "🥥", "🥝", "🍅", "🥑", "🍔", "🍕", "🌭", "🍟", "🍿", "🍜", "🍣", "🍱", "🍰", "🍪", "🍩", "🍫", "☕"],
        ["⚽", "🏀", "🏈", "⚾", "🎾", "🏐", "🏆", "🥇", "🎮", "🎲", "🎯", "🎨", "🎵", "🎶", "🎤", "🎧", "🎬", "📚", "✈️", "🚗", "🚀", "🌈", "☀️", "🌙", "⭐", "🌟", "🎁", "🎈", "🎂", "🥳"]
    ]

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Qt.rgba(0.10, 0.13, 0.18, 0.96)
        border.color: Theme.glassHighlight
        border.width: 1
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: parent.radius - 1
            color: "transparent"
            border.color: Qt.rgba(1, 1, 1, 0.06)
            border.width: 1
        }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 180; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120; easing.type: Easing.InCubic }
            NumberAnimation { property: "scale"; from: 1; to: 0.98; duration: 120; easing.type: Easing.InCubic }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingS

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "选择表情"
                color: Theme.primaryText
                font.pixelSize: Theme.fontBodyLarge
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Label {
                text: "点击插入"
                color: Theme.secondaryText
                font.pixelSize: Theme.fontCaption
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            Repeater {
                model: picker.categoryNames
                delegate: Button {
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: 30
                    text: picker.categoryNames[index]
                    flat: true
                    padding: 2
                    contentItem: Text {
                        text: parent.text
                        color: picker.categoryIndex === index ? Theme.accent : Theme.secondaryText
                        font.pixelSize: Theme.fontCaption
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: picker.categoryIndex === index ? Qt.rgba(0.35, 0.62, 1.0, 0.16) : "transparent"
                    }
                    onClicked: picker.categoryIndex = index
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSoft }

        GridView {
            id: emojiGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            cellWidth: 48
            cellHeight: 48
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: picker.categoryEmojis[picker.categoryIndex]
            delegate: Rectangle {
                required property string modelData
                width: emojiGrid.cellWidth
                height: emojiGrid.cellHeight
                radius: Theme.radiusMedium
                color: emojiMouse.containsMouse ? Qt.rgba(0.96, 0.98, 1.0, 0.14) : "transparent"
                scale: emojiMouse.pressed ? 0.92 : emojiMouse.containsMouse ? 1.05 : 1
                Behavior on color { ColorAnimation { duration: Theme.animationFast } }
                Behavior on scale { NumberAnimation { duration: Theme.animationFast; easing.type: Easing.OutCubic } }
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    font.pixelSize: 25
                }
                MouseArea {
                    id: emojiMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: picker.emojiSelected(modelData)
                }
            }
            ScrollBar.vertical: AppScrollBar { }
        }
    }
}
