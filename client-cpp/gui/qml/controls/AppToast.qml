import QtQuick
import QtQuick.Controls
import LanChatGui

AppPopup {
    id: root

    property alias text: messageLabel.text
    property string tone: "info"
    property int displayDuration: 2600
    property var pending: []

    parent: Overlay.overlay
    modal: false
    focus: false
    closePolicy: Popup.NoAutoClose
    padding: Theme.spacingM
    width: Math.min(420, Math.max(220, messageLabel.implicitWidth + Theme.spacingXL * 2))
    height: Math.max(44, messageLabel.implicitHeight + Theme.spacingM * 2)
    x: parent ? parent.width - width - Theme.spacingXL : 0
    y: Theme.spacingXL
    popupStartScale: 0.985

    function show(message, messageTone) {
        if (!message || message.trim().length === 0) return
        const nextTone = messageTone || "info"
        if (visible) {
            if (root.text === message && root.tone === nextTone) {
                hideTimer.restart()
                return
            }
            if (pending.length < 3) pending.push({message: message, tone: nextTone})
            return
        }
        root.text = message
        root.tone = nextTone
        open()
        hideTimer.restart()
    }

    Timer {
        id: hideTimer
        interval: root.displayDuration
        onTriggered: root.close()
    }

    onClosed: {
        if (pending.length === 0) return
        const next = pending.shift()
        Qt.callLater(function() { root.show(next.message, next.tone) })
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.close()
    }

    background: Rectangle {
        radius: Theme.radiusMedium
        color: Qt.rgba(0.10, 0.13, 0.18, 0.97)
        border.color: root.tone === "error" ? Theme.danger
                     : root.tone === "success" ? Theme.success : Theme.accent
        border.width: 1
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 2
            radius: parent.radius
            color: root.tone === "error" ? Theme.danger
                  : root.tone === "success" ? Theme.success : Theme.accent
            opacity: 0.75
        }
    }

    contentItem: Label {
        id: messageLabel
        color: Theme.primaryText
        font.pixelSize: Theme.fontBody
        wrapMode: Text.WordWrap
        verticalAlignment: Text.AlignVCenter
    }
}
