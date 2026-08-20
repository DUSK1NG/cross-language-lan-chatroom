import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    minimumWidth: 1000
    minimumHeight: 620
    flags: Qt.FramelessWindowHint | Qt.Window
    visible: true
    title: "LAN Chat"
    color: Theme.background

    property string currentPage: "mode"
    property string selectedMode: ""

    function openChat(mode) {
        selectedMode = mode
        currentPage = "chat"
    }

    function openRemoteSetup(mode) {
        selectedMode = mode
        currentPage = "connect"
    }
    function openHostSetup() {
        selectedMode = "Local Host"
        currentPage = "host"
    }

    function openSettings() { currentPage = "settings" }
    function returnToChat() { currentPage = "chat" }

    Rectangle {
        anchors.fill: parent
        z: -2
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#14181d" }
            GradientStop { position: 0.52; color: "#11151a" }
            GradientStop { position: 1.0; color: "#0f1216" }
        }
    }

    Rectangle {
        width: 420
        height: 260
        radius: 180
        x: -120
        y: 80
        z: -1
        color: Theme.glowBlue
        opacity: 0.34
    }

    Rectangle {
        width: 360
        height: 240
        radius: 170
        anchors.right: parent.right
        y: 90
        z: -1
        color: Theme.glowViolet
        opacity: 0.28
    }

    TitleBar {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.spacingXXL + Theme.spacingL
        onCloseRequested: window.close()
    }

    Loader {
        id: pageLoader
        z: 1
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: parent.width
        height: Math.max(0, parent.height - titleBar.height)
        visible: true
        opacity: 1
        x: 0
        Behavior on opacity {
            NumberAnimation { duration: Theme.animationNormal; easing.type: Easing.OutCubic }
        }
        Behavior on x {
            NumberAnimation { duration: Theme.animationNormal; easing.type: Easing.OutCubic }
        }
        source: currentPage === "mode" ? "pages/ModeSelectionPage.qml" :
                currentPage === "connect" ? "pages/ConnectionPage.qml" :
                currentPage === "host" ? "pages/HostSetupPage.qml" :
                currentPage === "settings" ? "pages/SettingsPage.qml" : "pages/ChatPage.qml"
        onLoaded: {
            if (!item) return
            item.width = pageLoader.width
            item.height = pageLoader.height
            if (currentPage === "mode") {
                item.modeSelected.connect(function(mode) {
                    if (mode === "Remote Server" || mode === "Guest") {
                        window.openRemoteSetup(mode)
                    } else if (mode === "Local Host") {
                        window.openHostSetup()
                    } else {
                        window.openChat(mode)
                    }
                })
            } else if (currentPage === "chat") {
                item.modeName = window.selectedMode
                item.settingsRequested.connect(window.openSettings)
            } else if (currentPage === "settings") {
                item.backRequested.connect(window.returnToChat)
            } else if (currentPage === "connect") {
                item.modeName = window.selectedMode
                item.backRequested.connect(function() { window.currentPage = "mode" })
            } else if (currentPage === "host") {
                item.backRequested.connect(function() { window.currentPage = "mode" })
            }
        }
    }

    Connections {
        target: chatController
        function onConnectedChanged() {
            if (chatController.connected) window.openChat(window.selectedMode)
        }
    }
}
