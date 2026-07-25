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

    // Parsed subtitle tracks. Independent of mpv by necessity -- see
    // SubtitleExtractor.
    SubtitleManager {
        id: subs
        onLoaded: {
            // Land on the first track that actually has text.
            root.currentTrack = -1
            for (var i = 0; i < subs.tracks.length; ++i) {
                if (subs.tracks[i].browsable) {
                    root.currentTrack = subs.tracks[i].id
                    break
                }
            }
        }
    }

    property int currentTrack: -1
    // Snapshot of the selected track's rows, taken on the GUI thread. Fine at
    // feature-film scale (~110 ms for 40k cues) but it does stall: 200k cues cost
    // ~600 ms. Milestone 2's QAbstractListModel removes the copy entirely.
    property var currentLines: currentTrack >= 0 ? subs.lines(currentTrack) : []

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
                        if (initialFile !== "") {
                            mpv.loadFile(initialFile)
                            subs.load(initialFile)
                        }
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

        // ---- docked subtitle browser -----------------------------------
        // Milestone 1 readout: real parsed tracks and rows, but still driven by
        // plain QVariantList snapshots. Search, tabs and auto-follow arrive with
        // the QAbstractListModel in milestone 2.
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

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12

                        Label {
                            color: "#d0d0dc"
                            text: "Subtitles"
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            color: subs.busy ? "#c8a45c" : "#6a6a7a"
                            font.pixelSize: 11
                            text: subs.status
                        }
                    }
                }

                // One button per track. Becomes a TabBar once tracks carry their
                // own models.
                Flow {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    spacing: 6
                    visible: subs.tracks.length > 0

                    Repeater {
                        model: subs.tracks

                        Button {
                            required property var modelData
                            text: modelData.label + " (" + modelData.lineCount + ")"
                            enabled: modelData.browsable
                            checkable: true
                            checked: root.currentTrack === modelData.id
                            font.pixelSize: 11
                            padding: 4
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.codec + " · " + modelData.kind
                                          + (modelData.sidecar ? " · sidecar " + modelData.source : "")
                                          + (modelData.note !== "" ? "\n" + modelData.note : "")
                            onClicked: root.currentTrack = modelData.id
                        }
                    }
                }

                TextField {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    placeholderText: "search…"
                    enabled: false
                }

                ListView {
                    id: lineList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 8
                    clip: true
                    spacing: 2
                    model: root.currentLines

                    ScrollBar.vertical: ScrollBar {}

                    delegate: ItemDelegate {
                        required property var modelData
                        width: lineList.width
                        // Clicking a row seeks there. testclip has a burned-in
                        // timecode, so the frame that lands is a direct check on
                        // the parsed timestamp.
                        onClicked: mpv.seek(modelData.startMs / 1000)

                        contentItem: ColumnLayout {
                            spacing: 1
                            Label {
                                color: "#6f6f85"
                                font.pixelSize: 10
                                font.family: "monospace"
                                text: modelData.start
                            }
                            Label {
                                Layout.fillWidth: true
                                color: "#d0d0dc"
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                text: modelData.text
                            }
                        }
                    }

                    Label {
                        anchors.fill: parent
                        visible: lineList.count === 0
                        color: "#6a6a7a"
                        wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignTop
                        text: subs.tracks.length === 0
                              ? "No subtitle tracks in this file."
                              : "Select a track above."
                    }
                }
            }
        }
    }
}
