import QtQuick
import QtQuick.Controls
import LanChatGui

Rectangle {
    id: root
    property string displayName: "?"
    property string imageSource: ""
    property int size: 32
    width: size
    height: size
    radius: Theme.radiusRound
    color: Theme.surfaceRaised
    border.color: Theme.borderSoft
    border.width: 1

    Image {
        anchors.fill: parent
        visible: root.imageSource.length > 0
        source: root.imageSource
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
    Label {
        anchors.centerIn: parent
        visible: root.imageSource.length === 0
        text: root.displayName.length > 0 ? root.displayName.slice(0, 1).toUpperCase() : "?"
        color: Theme.primaryText
        font.pixelSize: Math.max(12, root.size * 0.42)
        font.weight: Font.DemiBold
    }
}
