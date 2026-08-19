import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    color: "#20232b"
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
            text: "Qt Quick Preview"
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
        background: null
        onClicked: root.closeRequested()
    }
}
