import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Rectangle {
    id: root
    property string title: "# lobby"
    property string subtitle: ""
    property bool showMembersButton: false
    signal settingsRequested()
    signal membersRequested()
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
        IconButton {
            visible: root.showMembersButton
            iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/users.svg"
            tooltipText: "在线成员"
            onClicked: root.membersRequested()
        }
        IconButton {
            iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/settings.svg"
            onClicked: root.settingsRequested()
        }
    }
}
