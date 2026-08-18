import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

RowLayout {
    id: root
    property string title: "# lobby"
    property string subtitle: ""
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
    Label { text: "Mock UI"; color: Theme.secondaryText; font.pixelSize: 12 }
}
