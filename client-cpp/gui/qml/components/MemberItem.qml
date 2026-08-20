import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

RowLayout {
    id: root
    property string displayName: ""
    property string userCode: ""
    property bool online: true
    property bool admin: false
    signal userSelected()
    height: 38
    spacing: Theme.spacingS
    scale: mouse.pressed ? 0.985 : 1.0
    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

    Avatar { displayName: root.displayName; size: 28 }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        Label { text: displayName; color: Theme.primaryText; font.pixelSize: Theme.fontCaption; font.weight: Font.Medium }
        Label { text: "#" + userCode; color: Theme.secondaryText; font.pixelSize: 10 }
    }
    Badge {
        visible: root.admin
        text: "ADMIN"
        badgeColor: Theme.accent
    }
    Rectangle {
        width: 7
        height: 7
        radius: Theme.radiusRound
        color: root.online ? Theme.success : Theme.secondaryText
    }
    MouseArea {
        anchors.fill: parent
        onClicked: root.userSelected()
    }
}
