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
                    trackTabs.currentIndex = i
                    break
                }
            }
        }
    }

    property int currentTrack: -1
    // View row of the cue playing right now, or -1 when playback is before the
    // first cue or that cue is filtered out.
    property int currentRow: -1

    // Rows of the selected track, filtered by the search box. Switching tracks
    // just repoints the proxy at another model -- no rows are copied, which is
    // the whole reason the QVariantList snapshot is gone.
    SubtitleFilterModel {
        id: lines
        sourceModel: subs.tracks.length > 0 ? subs.model(root.currentTrack) : null
    }

    // Auto-follow. Cheap enough to run on every position tick: rowAt() is a
    // binary search plus a proxy row mapping, and the view is only touched when
    // the row actually changes.
    function syncFollow() {
        var row = lines.rowAt(Math.round(mpv.position * 1000))
        if (row === root.currentRow)
            return
        root.currentRow = row
        if (followToggle.checked && row >= 0)
            lineList.positionViewAtIndex(row, ListView.Contain)
    }

    Connections {
        target: mpv
        function onPositionChanged() { root.syncFollow() }
    }

    Connections {
        // Filtering or a track switch renumbers the rows, so the highlight has
        // to be recomputed even though playback has not moved.
        target: lines
        function onCountChanged() {
            root.currentRow = -1
            // Deferred: countChanged arrives mid rows-inserted/removed, and
            // scrolling the view from inside its own model update is asking for
            // trouble.
            Qt.callLater(root.syncFollow)
        }
    }

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
        // One tab per track, incremental search, click-to-seek, and auto-follow
        // with a toggle so scrolling by hand does not fight playback.
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
                            // While searching, say how much of the track is
                            // showing; otherwise the parse result.
                            text: lines.pattern !== ""
                                  ? lines.count + " of " + lines.sourceCount + " lines"
                                  : subs.status
                        }
                    }
                }

                TabBar {
                    id: trackTabs
                    Layout.fillWidth: true
                    visible: subs.tracks.length > 0

                    Repeater {
                        model: subs.tracks

                        TabButton {
                            required property var modelData
                            text: modelData.label + " (" + modelData.lineCount + ")"
                            enabled: modelData.browsable
                            width: Math.max(implicitWidth, 72)
                            font.pixelSize: 11
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.codec + " · " + modelData.kind
                                          + (modelData.sidecar ? " · sidecar " + modelData.source : "")
                                          + (modelData.note !== "" ? "\n" + modelData.note : "")
                        }
                    }

                    onCurrentIndexChanged: {
                        var track = subs.tracks[currentIndex]
                        if (track !== undefined && track.browsable)
                            root.currentTrack = track.id
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    spacing: 8

                    TextField {
                        id: searchField
                        Layout.fillWidth: true
                        placeholderText: "search…"
                        enabled: lines.sourceCount > 0
                        font.pixelSize: 12
                        // Filtering walks every cue, so a 200k-line track would
                        // do that work on each keystroke. Coalesce instead.
                        onTextChanged: searchDebounce.restart()
                        Keys.onEscapePressed: text = ""

                        Timer {
                            id: searchDebounce
                            interval: 150
                            onTriggered: lines.pattern = searchField.text
                        }
                    }

                    Button {
                        id: followToggle
                        text: "Follow"
                        checkable: true
                        checked: true
                        font.pixelSize: 11
                        padding: 6
                        ToolTip.visible: hovered
                        ToolTip.text: "Scroll the list to the line playing now.\n"
                                      + "Turns itself off if you drag the list."
                        // Coming back on should jump to the current line rather
                        // than wait for the next cue boundary.
                        onCheckedChanged: {
                            if (checked && root.currentRow >= 0)
                                lineList.positionViewAtIndex(root.currentRow, ListView.Contain)
                        }
                    }
                }

                ListView {
                    id: lineList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 8
                    clip: true
                    spacing: 2
                    model: lines

                    ScrollBar.vertical: ScrollBar {}

                    // Dragging the list is a statement of intent: stop yanking
                    // the viewport back to the playing line.
                    onDragStarted: followToggle.checked = false

                    delegate: ItemDelegate {
                        id: lineRow
                        required property int index
                        required property var model
                        readonly property bool current: index === root.currentRow

                        width: lineList.width
                        // Clicking a row seeks there. testclip has a burned-in
                        // timecode, so the frame that lands is a direct check on
                        // the parsed timestamp.
                        onClicked: mpv.seek(model.startMs / 1000)

                        background: Rectangle {
                            color: lineRow.current ? "#23324a"
                                                   : (lineRow.hovered ? "#1b1b25" : "transparent")
                            border.color: lineRow.current ? "#42618f" : "transparent"
                            radius: 3
                        }

                        contentItem: ColumnLayout {
                            spacing: 1
                            Label {
                                color: lineRow.current ? "#9fb6dc" : "#6f6f85"
                                font.pixelSize: 10
                                font.family: "monospace"
                                text: lineRow.model.start
                            }
                            Label {
                                Layout.fillWidth: true
                                color: lineRow.current ? "#ffffff" : "#d0d0dc"
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                text: lineRow.model.text
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
                              : lines.pattern !== ""
                                ? "No lines match “" + lines.pattern + "”."
                                : "Select a track above."
                    }
                }
            }
        }
    }
}
