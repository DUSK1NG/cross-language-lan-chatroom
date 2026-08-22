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
            AppButton { text: "‹ 返回"; onClicked: root.backRequested() }
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

        Label {
            text: "性能 / 图形信息"
            color: Theme.accent
            font.pixelSize: 13
            font.weight: Font.DemiBold
            Layout.topMargin: 12
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "性能等级"; color: Theme.primaryText; Layout.fillWidth: true }
            ComboBox {
                id: performanceMode
                model: ["自动", "高性能", "均衡", "省电"]
                implicitWidth: 150
                Component.onCompleted: syncMode()
                function syncMode() {
                    const modes = ["Automatic", "High", "Balanced", "Power Saving"]
                    currentIndex = Math.max(0, modes.indexOf(performanceProfile.mode))
                }
                onActivated: {
                    const modes = ["Automatic", "High", "Balanced", "Power Saving"]
                    performanceProfile.mode = modes[index]
                }
                Connections {
                    target: performanceProfile
                    function onModeChanged() { performanceMode.syncMode() }
                }
            }
        }
        Label {
            text: "当前生效：" + performanceProfile.effectiveMode
            color: Theme.secondaryText
            font.pixelSize: Theme.fontCaption
        }

        GridLayout {
            columns: 2
            columnSpacing: 28
            rowSpacing: 8
            Layout.fillWidth: true

            Label { text: "渲染 API"; color: Theme.secondaryText }
            Label { text: graphicsInfo.graphicsApi; color: Theme.primaryText; Layout.fillWidth: true }

            Label { text: "加速状态"; color: Theme.secondaryText }
            Label {
                text: graphicsInfo.hardwareAcceleration ? "硬件加速" : (graphicsInfo.softwareRendering ? "软件渲染" : "未知")
                color: Theme.primaryText
                Layout.fillWidth: true
            }

            Label { text: "渲染器 / 厂商"; color: Theme.secondaryText }
            Label {
                text: graphicsInfo.renderer + " / " + graphicsInfo.vendor
                color: Theme.primaryText
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label { text: "屏幕"; color: Theme.secondaryText }
            Label { text: graphicsInfo.resolution; color: Theme.primaryText; Layout.fillWidth: true }

            Label { text: "刷新率 / DPI"; color: Theme.secondaryText }
            Label {
                text: graphicsInfo.refreshRate > 0 ? (graphicsInfo.refreshRate.toFixed(1) + " Hz / " + graphicsInfo.dpi.toFixed(1)) : "Unknown"
                color: Theme.primaryText
                Layout.fillWidth: true
            }

            Label { text: "自动策略原因"; color: Theme.secondaryText }
            Label { text: performanceProfile.automaticReason; color: Theme.primaryText; Layout.fillWidth: true; elide: Text.ElideRight }

            Label { text: "观测帧率"; color: Theme.secondaryText }
            Label {
                text: performanceProfile.observedFrameCount > 0 ? (performanceProfile.observedFps.toFixed(1) + " FPS") : "等待样本"
                color: Theme.primaryText
                Layout.fillWidth: true
            }

            Label { text: "P95 / 最大帧耗时"; color: Theme.secondaryText }
            Label {
                text: performanceProfile.observedFrameCount > 0 ? (performanceProfile.observedP95FrameMs.toFixed(2) + " / " + performanceProfile.observedMaxFrameMs.toFixed(2) + " ms") : "--"
                color: Theme.primaryText
                Layout.fillWidth: true
            }
        }

        Item { Layout.fillHeight: true }
    }
}
