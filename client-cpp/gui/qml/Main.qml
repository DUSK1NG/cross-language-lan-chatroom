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
        if (chatPageLoader.item) chatPageLoader.item.modeName = mode
    }

    function openRemoteSetup(mode) {
        selectedMode = mode
        if (connectPageLoader.item) connectPageLoader.item.modeName = mode
        currentPage = "connect"
    }
    function openHostSetup() {
        if (!hostAvailable) return
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
        visible: performanceProfile.gradientsEnabled
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
        visible: performanceProfile.effectsEnabled
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
        visible: performanceProfile.effectsEnabled
    }

    TitleBar {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.spacingXXL + Theme.spacingL
        onCloseRequested: window.close()
    }

    Item {
        id: pageHost
        z: 1
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true

        // Keep page instances alive. Switching settings/chat now only toggles
        // visibility and does not rebuild the message ListView and delegates.
        Loader {
            id: modePageLoader
            anchors.fill: parent
            source: "pages/ModeSelectionPage.qml"
            visible: currentPage === "mode"
            onLoaded: {
                if (!item) return
                item.modeSelected.connect(function(mode) {
                    if (mode === "Remote Server" || mode === "Guest") {
                        window.openRemoteSetup(mode)
                    } else if (mode === "Local Host") {
                        window.openHostSetup()
                    } else {
                        window.openChat(mode)
                    }
                })
            }
        }

        Loader {
            id: connectPageLoader
            anchors.fill: parent
            source: "pages/ConnectionPage.qml"
            visible: currentPage === "connect"
            onLoaded: {
                if (!item) return
                item.modeName = window.selectedMode
                item.backRequested.connect(function() { window.currentPage = "mode" })
            }
        }

        Loader {
            id: hostPageLoader
            anchors.fill: parent
            source: "pages/HostSetupPage.qml"
            visible: currentPage === "host"
            onLoaded: {
                if (!item) return
                item.backRequested.connect(function() { window.currentPage = "mode" })
            }
        }

        Loader {
            id: settingsPageLoader
            anchors.fill: parent
            source: "pages/SettingsPage.qml"
            visible: currentPage === "settings"
            onLoaded: {
                if (!item) return
                item.backRequested.connect(window.returnToChat)
            }
        }

        Loader {
            id: chatPageLoader
            anchors.fill: parent
            source: "pages/ChatPage.qml"
            visible: currentPage === "chat"
            onLoaded: {
                if (!item) return
                item.modeName = window.selectedMode
                item.settingsRequested.connect(window.openSettings)
            }
        }
    }

    Loader {
        id: performanceOverlayLoader
        anchors.top: titleBar.bottom
        anchors.right: parent.right
        width: 262
        height: 190
        z: 100
        active: typeof performanceSampler !== "undefined"
        source: "controls/PerformanceOverlay.qml"
        onLoaded: if (item) { item.sampler = performanceSampler; item.profile = performanceProfile }
    }

    Connections {
        target: chatController
        function onConnectedChanged() {
            if (chatController.connected) window.openChat(window.selectedMode)
        }
    }
}
