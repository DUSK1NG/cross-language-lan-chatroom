import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LanChatGui

Item {
    id: root
    signal backRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 32
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            GlassButton { text: "‹ 返回"; onClicked: root.backRequested() }
            Label { text: "设置"; color: Theme.primaryText; font.pixelSize: 24; font.weight: Font.DemiBold }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        Label { text: "界面"; color: Theme.accent; font.pixelSize: 13; font.weight: Font.DemiBold }
        RowLayout {
            Layout.fillWidth: true
            Label { text: "深色主题"; color: Theme.primaryText; Layout.fillWidth: true }
            Switch { checked: true; enabled: false }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: "显示发送时间"; color: Theme.primaryText; Layout.fillWidth: true }
            Switch { checked: true }
        }

        Label { text: "连接"; color: Theme.accent; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.topMargin: 12 }
        Label { text: "当前模式：Go TLS Server / 本地 Host"; color: Theme.secondaryText; font.pixelSize: 13 }
        Label { text: "连接配置由启动页面管理"; color: Theme.secondaryText; font.pixelSize: 12 }

        Item { Layout.fillHeight: true }
    }
}
