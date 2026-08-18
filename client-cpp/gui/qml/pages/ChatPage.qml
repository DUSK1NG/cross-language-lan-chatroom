import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Item {
    id: root
    property string modeName: "Mock Mode"

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            color: Theme.surface

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                Label {
                    text: "聊天空间"
                    color: Theme.primaryText
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Label { text: "房间"; color: Theme.secondaryText; font.pixelSize: 12 }
                Repeater {
                    model: ["# lobby", "# study", "# gaming"]
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        height: 34
                        radius: 6
                        color: index === 0 ? Theme.surfaceRaised : "transparent"
                        Label {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            verticalAlignment: Text.AlignVCenter
                            text: modelData
                            color: index === 0 ? Theme.primaryText : Theme.secondaryText
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Label { text: modeName; color: Theme.accent; font.pixelSize: 12 }
                Label { text: "Mock User#A001"; color: Theme.primaryText; font.pixelSize: 13 }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "# lobby"; color: Theme.primaryText; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    Label { text: "Mock UI"; color: Theme.secondaryText; font.pixelSize: 12 }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                Item { Layout.fillHeight: true; Layout.fillWidth: true }

                Rectangle {
                    Layout.fillWidth: true
                    height: 56
                    radius: 10
                    color: Theme.surface
                    border.color: Theme.border
                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        verticalAlignment: Text.AlignVCenter
                        text: "输入消息...（Phase 2 接入真实网络）"
                        color: Theme.secondaryText
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 230
            Layout.fillHeight: true
            color: Theme.surface
            Column {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                Label { text: "在线成员 · 3"; color: Theme.primaryText; font.pixelSize: 14; font.weight: Font.DemiBold }
                Repeater {
                    model: ["Alice", "Bob", "Chen"]
                    delegate: Label { text: "●  " + modelData; color: Theme.secondaryText; font.pixelSize: 13 }
                }
            }
        }
    }
}
