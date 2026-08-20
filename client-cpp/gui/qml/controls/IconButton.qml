import QtQuick
import QtQuick.Controls
import LanChatGui

ToolButton {
    id: root
    property string iconSource: ""
    property string tooltipText: ""
    implicitWidth: 34
    implicitHeight: 34
    hoverEnabled: true

    icon.source: root.iconSource
    icon.width: 18
    icon.height: 18

    background: Rectangle {
        radius: Theme.radiusMedium
        color: root.pressed ? Qt.rgba(0.96, 0.98, 1.0, 0.24)
                            : root.hovered ? Qt.rgba(0.96, 0.98, 1.0, 0.14)
                                           : "transparent"
        border.color: root.hovered ? Theme.border : "transparent"
        border.width: 1
        Behavior on color { ColorAnimation { duration: Theme.animationFast } }
    }

    ToolTip.visible: root.hovered && root.tooltipText.length > 0
    ToolTip.text: root.tooltipText
    ToolTip.delay: 450
}
