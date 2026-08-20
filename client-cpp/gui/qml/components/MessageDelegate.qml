import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Item {
    id: root
    property string displayName: ""
    property string userCode: ""
    property string time: ""
    property string content: ""
    property bool selfMessage: false
    property bool systemMessage: false
    property string messageId: ""
    property bool canRecall: false
    property bool hovered: hoverArea.containsMouse
    signal copyRequested(string content)
    signal quoteRequested(string displayName, string content)
    signal localDeleteRequested(string messageId)
    signal recallRequested(string messageId)
    signal profileRequested(string displayName, string userCode)
    height: systemMessage ? 34 : messageColumn.implicitHeight + Theme.spacingS

    Label {
        anchors.centerIn: parent
        visible: root.systemMessage
        text: root.content
        color: Theme.secondaryText
        font.pixelSize: 11
        font.italic: true
    }

    Column {
        id: messageColumn
        visible: !root.systemMessage
        anchors.left: root.selfMessage ? undefined : parent.left
        anchors.right: root.selfMessage ? parent.right : undefined
        width: Math.min(parent.width * 0.82, 720)
        spacing: Theme.spacingS

        Row {
            id: identityRow
            width: childrenRect.width
            height: 26
            anchors.right: root.selfMessage ? parent.right : undefined
            spacing: Theme.spacingS
            Avatar { visible: !root.selfMessage; displayName: root.displayName; size: 26 }
            Label { text: root.displayName; color: root.selfMessage ? Theme.accent : Theme.primaryText; font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
            Label { text: "#" + root.userCode + "  " + root.time; color: Theme.secondaryText; font.pixelSize: Theme.fontCaption; anchors.verticalCenter: parent.verticalCenter }
            Avatar { visible: root.selfMessage; displayName: root.displayName; size: 26 }
            TapHandler { onTapped: root.profileRequested(root.displayName, root.userCode) }
        }
        Rectangle {
            anchors.right: root.selfMessage ? parent.right : undefined
            width: Math.min(messageText.implicitWidth + 28, messageColumn.width)
            height: messageText.implicitHeight + 20
            radius: Theme.radiusMedium
            color: root.selfMessage ? Theme.selfBubble : Theme.otherBubble
            border.color: root.selfMessage ? "transparent" : Theme.borderSoft
            border.width: root.selfMessage ? 0 : 1
            opacity: root.hovered ? 1.0 : 0.97
            Behavior on opacity { NumberAnimation { duration: 140 } }
            Text {
                id: messageText
                anchors.centerIn: parent
                width: Math.min(implicitWidth, messageColumn.width - 28)
                text: root.content
                wrapMode: Text.Wrap
                horizontalAlignment: root.selfMessage ? Text.AlignRight : Text.AlignLeft
                color: Theme.primaryText
                font.pixelSize: Theme.fontBody
            }
        }
        Row {
            visible: root.hovered
            anchors.right: root.selfMessage ? parent.right : undefined
            spacing: Theme.spacingS
            AppButton { compact: true; text: "复制"; onClicked: root.copyRequested(root.content) }
            AppButton { compact: true; text: "更多"; onClicked: morePopup.open() }
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    Popup {
        id: morePopup
        width: 150
        height: root.canRecall ? 178 : 140
        x: root.selfMessage ? parent.width - width : 0
        y: Math.max(0, root.height - height)
        padding: Theme.spacingXS
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: Theme.radiusMedium
            color: Qt.rgba(0.10, 0.13, 0.18, 0.98)
            border.color: Theme.glassHighlight
            border.width: 1
        }
        Column {
            anchors.fill: parent
            spacing: Theme.spacingXS
            AppButton { width: parent.width; compact: true; text: "复制消息"; onClicked: { root.copyRequested(root.content); morePopup.close() } }
            AppButton { width: parent.width; compact: true; text: "引用回复"; onClicked: { root.quoteRequested(root.displayName, root.content); morePopup.close() } }
            AppButton { width: parent.width; compact: true; text: "删除消息"; danger: true; onClicked: { root.localDeleteRequested(root.messageId); morePopup.close() } }
            AppButton { visible: root.canRecall; width: parent.width; compact: true; text: "管理员撤回"; danger: true; onClicked: { root.recallRequested(root.messageId); morePopup.close() } }
        }
    }
}
