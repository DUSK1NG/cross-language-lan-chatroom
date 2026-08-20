import QtQuick
import QtQuick.Controls
import LanChatGui

TextField {
    id: root
    property string iconSource: ""
    leftPadding: iconSource.length > 0 ? 38 : Theme.spacingM
    rightPadding: Theme.spacingM
    topPadding: Theme.spacingS
    bottomPadding: Theme.spacingS
    color: Theme.primaryText
    placeholderTextColor: Theme.secondaryText
    selectByMouse: true

    Image {
        visible: root.iconSource.length > 0
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingM
        anchors.verticalCenter: parent.verticalCenter
        source: root.iconSource
        sourceSize.width: Theme.fontBodyLarge
        sourceSize.height: Theme.fontBodyLarge
    }

    background: Rectangle {
        radius: Theme.radiusMedium
        color: root.activeFocus ? Qt.rgba(0.96, 0.98, 1.0, 0.14)
                                : Qt.rgba(0.90, 0.93, 0.97, 0.08)
        border.color: root.activeFocus ? Theme.accent : Theme.borderSoft
        border.width: root.activeFocus ? 2 : 1
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: root.activeFocus ? Theme.glassHighlight : "transparent"
        }
        Behavior on color { ColorAnimation { duration: Theme.animationNormal } }
        Behavior on border.color { ColorAnimation { duration: Theme.animationNormal } }
    }
}
