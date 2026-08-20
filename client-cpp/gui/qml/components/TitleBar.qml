import QtQuick
import QtQuick.Controls
import LanChatGui

Rectangle {
    id: root
    color: Theme.surface
    signal closeRequested()

    Row {
        anchors.fill: parent
        anchors.leftMargin: 18
        spacing: 10

        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: "LAN CHAT"
            color: "#f1f3f5"
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: "Qt 6 Desktop Client"
            color: "#9aa3b2"
            font.pixelSize: 12
        }
    }

    ToolButton {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: 8
        text: "×"
        font.pixelSize: 20
        contentItem: Text {
            text: parent.text
            color: parent.hovered ? "#ef7d8a" : "#9aa3b2"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 8
            color: parent.pressed
                   ? Qt.rgba(0.82, 0.28, 0.36, 0.58)
                   : parent.hovered ? Qt.rgba(0.88, 0.34, 0.42, 0.30)
                                    : Qt.rgba(0.78, 0.86, 0.98, 0.07)
            border.color: parent.hovered ? Qt.rgba(1.0, 0.58, 0.64, 0.60) : Theme.borderSoft
            border.width: 1
            Behavior on color { ColorAnimation { duration: 160 } }
            Behavior on border.color { ColorAnimation { duration: 160 } }
        }
        scale: pressed ? 0.95 : hovered ? 1.04 : 1.0
        Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        onClicked: root.closeRequested()
    }
}
