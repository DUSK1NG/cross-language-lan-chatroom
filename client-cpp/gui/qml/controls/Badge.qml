import QtQuick
import QtQuick.Controls
import LanChatGui

Rectangle {
    id: root
    property string text: ""
    property color badgeColor: Theme.accent
    implicitWidth: badgeLabel.implicitWidth + Theme.spacingM
    implicitHeight: 20
    radius: Theme.radiusRound
    color: Qt.rgba(root.badgeColor.r, root.badgeColor.g, root.badgeColor.b, 0.18)
    border.color: Qt.rgba(root.badgeColor.r, root.badgeColor.g, root.badgeColor.b, 0.42)
    border.width: 1

    Label {
        id: badgeLabel
        anchors.centerIn: parent
        text: root.text
        color: root.badgeColor
        font.pixelSize: Theme.fontCaption
        font.weight: Font.DemiBold
    }
}
