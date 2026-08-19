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

    TitleBar {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 48
        onCloseRequested: window.close()
    }

    Loader {
        id: pageLoader
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        source: currentPage === "mode" ? "pages/ModeSelectionPage.qml" :
                currentPage === "connect" ? "pages/ConnectionPage.qml" :
                currentPage === "host" ? "pages/HostSetupPage.qml" :
                currentPage === "settings" ? "pages/SettingsPage.qml" : "pages/ChatPage.qml"
        onLoaded: {
            if (!item) return
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
