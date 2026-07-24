import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CustomMediaPlayer

ApplicationWindow {
    id: root
    width: 1180
    height: 700
    visible: true
    title: "custom media player"
    color: "#0e0e12"

    function fmt(t) {
        if (!isFinite(t) || t < 0)
            return "--:--"
        var s = Math.floor(t % 60)
        var m = Math.floor(t / 60) % 60
        var h = Math.floor(t / 3600)
        var mm = (m < 10 && h > 0 ? "0" : "") + m
        var ss = (s < 10 ? "0" : "") + s
        return (h > 0 ? h + ":" : "") + mm + ":" + ss
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- video + transport ----------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "black"

                MpvObject {
                    id: mpv
                    anchors.fill: parent

                    Component.onCompleted: {
                        if (initialFile !== "")
                            mpv.loadFile(initialFile)
                    }
                    onLogMessage: (text) => console.log("[mpv]", text)
                }

                // Proves QML composites over the video surface -- the whole
                // reason for the render API rather than --wid.
                Rectangle {
                    anchors.centerIn: parent
                    visible: mpv.duration <= 0
                    width: hint.implicitWidth + 32
                    height: hint.implicitHeight + 20
                    radius: 6
                    color: "#cc1c1c24"
                    border.color: "#3a3a48"

                    Label {
                        id: hint
                        anchors.centerIn: parent
                        color: "#c8c8d4"
                        text: "no media loaded\npass a file path as argv[1]"
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 52
                color: "#16161d"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12

                    Button {
                        text: mpv.paused ? "Play" : "Pause"
                        enabled: mpv.duration > 0
                        onClicked: mpv.togglePause()
                    }

                    Label {
                        color: "#9a9aa8"
                        text: root.fmt(mpv.position)
                    }

                    Slider {
                        id: seekBar
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(mpv.duration, 0.1)
                        enabled: mpv.duration > 0
                        value: pressed ? value : mpv.position
                        onMoved: mpv.seek(value)
                    }

                    Label {
                        color: "#9a9aa8"
                        text: root.fmt(mpv.duration)
                    }
                }
            }
        }

        // ---- docked subtitle browser (shell only) ----------------------
        Rectangle {
            Layout.preferredWidth: 340
            Layout.fillHeight: true
            color: "#12121a"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    color: "#1b1b25"
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        color: "#d0d0dc"
                        text: "Subtitles"
                        font.bold: true
                    }
                }

                TextField {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    placeholderText: "search…"
                    enabled: false
                }

                Label {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 12
                    color: "#6a6a7a"
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignTop
                    text: "Not wired up yet.\n\nSubtitle text does not come from mpv — it only "
                          + "exposes the currently displayed line. Tracks get parsed separately "
                          + "with libavformat/libavcodec, then listed here as timestamped rows "
                          + "with click-to-seek and auto-follow."
                }
            }
        }
    }
}
