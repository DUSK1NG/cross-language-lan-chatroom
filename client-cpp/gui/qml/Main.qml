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
                currentPage === "settings" ? "pages/SettingsPage.qml" : "pages/ChatPage.qml"
        onLoaded: {
            if (!item) return
            if (currentPage === "chat") {
                item.modeName = window.selectedMode
                item.settingsRequested.connect(window.openSettings)
            } else if (currentPage === "settings") {
                item.backRequested.connect(window.returnToChat)
            }
        }
    }
}
