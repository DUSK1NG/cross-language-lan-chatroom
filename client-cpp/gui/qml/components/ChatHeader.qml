import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

RowLayout {
    id: root
    property string title: "# lobby"
    property string subtitle: ""
    signal settingsRequested()
    height: 42

    Label {
        text: root.title
        color: Theme.primaryText
        font.pixelSize: 18
        font.weight: Font.DemiBold
    }
    Label {
        text: root.subtitle
        color: Theme.secondaryText
        font.pixelSize: 12
        Layout.leftMargin: 8
    }
    Item { Layout.fillWidth: true }
    ToolButton {
        text: "⚙"
        background: null
        onClicked: root.settingsRequested()
        contentItem: Text {
            text: parent.text
            color: parent.hovered ? Theme.primaryText : Theme.secondaryText
            font.pixelSize: 18
        }
    }
}
