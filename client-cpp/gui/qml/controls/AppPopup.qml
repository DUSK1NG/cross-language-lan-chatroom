import QtQuick
import QtQuick.Controls
import LanChatGui

Popup {
    id: root

    property real popupStartScale: Theme.popupStartScale
    property int popupEnterDuration: Theme.popupEnterDuration
    property int popupExitDuration: Theme.popupExitDuration
    property real motionScale: typeof performanceProfile === "undefined"
                               ? 1.0 : performanceProfile.animationDurationScale

    transformOrigin: Item.Center
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Math.round(root.popupEnterDuration * root.motionScale)
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                property: "scale"
                from: root.popupStartScale
                to: 1
                duration: Math.round(root.popupEnterDuration * root.motionScale)
                easing.type: Easing.OutCubic
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Math.round(root.popupExitDuration * root.motionScale)
                easing.type: Easing.InCubic
            }
            NumberAnimation {
                property: "scale"
                from: 1
                to: root.popupStartScale
                duration: Math.round(root.popupExitDuration * root.motionScale)
                easing.type: Easing.InCubic
            }
        }
    }
}
