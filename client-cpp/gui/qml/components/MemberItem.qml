import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

RowLayout {
    property string displayName: ""
    property string userCode: ""
    property bool online: true
    property bool admin: false
    signal userSelected()
    height: 26
    spacing: 4

    Label {
        text: online ? "●" : "○"
        color: online ? Theme.success : Theme.secondaryText
        font.pixelSize: 9
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        Label { text: displayName; color: Theme.primaryText; font.pixelSize: 11 }
        Label { text: "#" + userCode; color: Theme.secondaryText; font.pixelSize: 9 }
    }
    Label {
        visible: admin
        text: "ADMIN"
        color: Theme.accent
        font.pixelSize: 7
        font.weight: Font.Bold
    }
    MouseArea {
        anchors.fill: parent
        onClicked: root.userSelected()
    }
}
