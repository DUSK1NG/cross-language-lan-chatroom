import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    anchors.fill: parent
    color: "#e6141a28"
    radius: 10
    border.color: "#6657f2d0"
    border.width: 1

    property var sampler
    property var profile

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 5

        Label {
            text: "DEBUG / FRAME TIME"
            color: "#57f2d0"
            font.pixelSize: 10
            font.letterSpacing: 1.2
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Label { text: "FPS  " + (root.sampler ? root.sampler.fps.toFixed(1) : "--"); color: "#f4f7fb"; font.pixelSize: 14 }
            Label { text: "N  " + (root.sampler ? root.sampler.sampleCount : "--"); color: "#9eabc0"; font.pixelSize: 12 }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: "P95"; color: "#9eabc0"; font.pixelSize: 11 }
            Label { text: root.sampler ? root.sampler.p95FrameMs.toFixed(2) + " ms" : "--"; color: "#f4f7fb"; font.pixelSize: 12; Layout.fillWidth: true }
            Label { text: "P99"; color: "#9eabc0"; font.pixelSize: 11 }
            Label { text: root.sampler ? root.sampler.p99FrameMs.toFixed(2) + " ms" : "--"; color: "#f4f7fb"; font.pixelSize: 12 }
        }

        Label {
            text: "MAX  " + (root.sampler ? root.sampler.maxFrameMs.toFixed(2) + " ms" : "--")
            color: "#ffb86b"
            font.pixelSize: 11
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#334f6a7a" }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: "AUTO"; color: "#57f2d0"; font.pixelSize: 11 }
            Label { text: root.profile ? root.profile.effectiveMode : "--"; color: "#f4f7fb"; font.pixelSize: 12; Layout.fillWidth: true }
            Label { text: root.profile ? root.profile.observedFrameCount : "--"; color: "#9eabc0"; font.pixelSize: 11 }
        }

        Label {
            text: root.profile ? (root.profile.observedP95FrameMs.toFixed(2) + " / " + root.profile.observedMaxFrameMs.toFixed(2) + " ms") : "--"
            color: "#f4f7fb"
            font.pixelSize: 11
        }

        Label {
            text: root.profile ? root.profile.automaticReason : "--"
            color: "#9eabc0"
            font.pixelSize: 10
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
