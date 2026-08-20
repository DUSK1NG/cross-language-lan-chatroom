import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Rectangle {
    id: root
    property string title: "# lobby"
    property string subtitle: ""
    signal settingsRequested()
    radius: 14
    color: Theme.panel
    border.color: Theme.border
    border.width: 1

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        radius: 1
        color: Theme.glassHighlight
        opacity: 0.85
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 10
        spacing: 8

        Label {
            text: root.title
            color: Theme.primaryText
            font.pixelSize: 19
            font.weight: Font.DemiBold
        }
        Label {
            text: root.subtitle
            color: Theme.secondaryText
            font.pixelSize: 12
            Layout.leftMargin: 4
        }
        Item { Layout.fillWidth: true }
        ToolButton {
            text: "⚙"
            background: Rectangle {
                radius: 9
                color: parent.hovered ? Theme.surfaceHover : Qt.rgba(1, 1, 1, 0.02)
                border.color: parent.hovered ? Theme.glassHighlight : "transparent"
                border.width: 1
            }
            onClicked: root.settingsRequested()
            contentItem: Text {
                text: parent.text
                color: parent.hovered ? Theme.primaryText : Theme.secondaryText
                font.pixelSize: 18
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
