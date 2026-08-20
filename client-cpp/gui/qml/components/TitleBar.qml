import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Rectangle {
    id: root
    property var appWindow: Window.window
    color: Theme.surface
    signal closeRequested()

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.glassHighlight
        opacity: 0.75
    }

    MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.LeftButton
        onPressed: {
            if (root.appWindow && mouse.button === Qt.LeftButton)
                root.appWindow.startSystemMove()
        }
        onDoubleClicked: {
            if (!root.appWindow) return
            if (root.appWindow.visibility === Window.Maximized)
                root.appWindow.showNormal()
            else
                root.appWindow.showMaximized()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingL
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingS

        Image {
            source: "qrc:/qt/qml/LanChatGui/qml/icons/network.svg"
            sourceSize.width: 22
            sourceSize.height: 22
            Layout.alignment: Qt.AlignVCenter
        }
        Label {
            text: "LAN Chat"
            color: Theme.primaryText
            font.pixelSize: Theme.fontBodyLarge
            font.weight: Font.DemiBold
        }
        Label {
            text: "Qt 6 Desktop Client"
            color: Theme.secondaryText
            font.pixelSize: Theme.fontCaption
            Layout.leftMargin: Theme.spacingS
        }
        Item { Layout.fillWidth: true }

        IconButton {
            iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/minimize.svg"
            tooltipText: "最小化"
            onClicked: if (root.appWindow) root.appWindow.showMinimized()
        }
        IconButton {
            iconSource: root.appWindow && root.appWindow.visibility === Window.Maximized
                        ? "qrc:/qt/qml/LanChatGui/qml/icons/restore.svg"
                        : "qrc:/qt/qml/LanChatGui/qml/icons/maximize.svg"
            tooltipText: root.appWindow && root.appWindow.visibility === Window.Maximized ? "还原" : "最大化"
            onClicked: {
                if (!root.appWindow) return
                if (root.appWindow.visibility === Window.Maximized)
                    root.appWindow.showNormal()
                else
                    root.appWindow.showMaximized()
            }
        }
        IconButton {
            iconSource: "qrc:/qt/qml/LanChatGui/qml/icons/close.svg"
            tooltipText: "关闭"
            onClicked: root.closeRequested()
        }
    }
}
