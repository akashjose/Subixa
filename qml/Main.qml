import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
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

    readonly property bool fullscreen: visibility === Window.FullScreen
    property bool panelVisible: true
    // Fullscreen means "just the picture", so the panel goes away -- but Tab can
    // bring it back without leaving fullscreen, which is the point of it being a
    // property rather than a binding on `fullscreen`.
    onFullscreenChanged: panelVisible = !fullscreen

    // Transport and panel stay put in a window; fullscreen hides them until the
    // mouse moves, then gives them a few seconds.
    readonly property bool showChrome: !fullscreen || chromeTimer.running

    Timer {
        id: chromeTimer
        interval: 2500
    }

    PlaybackHistory {
        id: history
    }

    // The file currently open, tracked because mpv's own path is not what the
    // history is keyed on and the outgoing file has to be saved before switching.
    property string currentFile: ""
    // Seconds to jump to once mpv reports the file loaded, or -1.
    property double pendingResume: -1

    // One way in for every source of a file: argv[1], the dialog, and a drop.
    function openFile(path) {
        if (path === "")
            return
        // Save where the outgoing file got to before its position is gone.
        root.rememberPosition()
        root.currentFile = path
        root.pendingResume = history.resumeFor(path)
        mpv.loadFile(path)
        subs.load(path)
    }

    function rememberPosition() {
        if (root.currentFile !== "" && mpv.duration > 0)
            history.remember(root.currentFile, mpv.position, mpv.duration)
    }

    Connections {
        target: mpv
        // Seeking has to wait for the file to be loaded; mpv has no position to
        // seek within before that.
        function onFileLoaded() {
            if (root.pendingResume > 0) {
                mpv.seek(root.pendingResume)
                root.pendingResume = -1
            }
        }
    }

    // Periodic save, so a crash or a kill -9 loses seconds rather than the whole
    // position. Only while playing: saving the same paused position repeatedly
    // just rewrites the file for nothing.
    Timer {
        interval: 5000
        repeat: true
        running: !mpv.paused && mpv.duration > 0
        onTriggered: root.rememberPosition()
    }

    onClosing: root.rememberPosition()

    function toggleFullscreen() {
        visibility = root.fullscreen ? Window.Windowed : Window.FullScreen
    }

    // Rows of the selected track, filtered by the search box. Switching tracks
    // just repoints the proxy at another model -- no rows are copied, which is
    // the whole reason the QVariantList snapshot is gone.
    SubtitleFilterModel {
        id: lines
        sourceModel: subs.tracks.length > 0 ? subs.model(root.currentTrack) : null
    }

    // Tells mpv to burn the track the panel is showing over the video. Until this
    // existed the two were independent, so you could read one language in the
    // panel while another rendered on screen with no way to reconcile them.
    //
    // Sidecars go by path and embedded tracks by ffmpeg stream index, because
    // mpv numbers its own tracks and neither numbering follows from the other.
    function applySubtitleSelection() {
        var track = subs.tracks[trackTabs.currentIndex]
        if (track === undefined || !track.browsable)
            return
        if (track.sidecar)
            mpv.selectSubtitleFile(track.sourcePath)
        else
            mpv.selectSubtitleStream(track.streamIndex)
    }

    // The other direction: picking a subtitle track from the transport menu moves
    // the panel to the matching tab, so the two agree no matter which was used.
    // Path comparison happens in C++ -- one file can be named relatively in the
    // extractor and absolutely by mpv.
    function syncPanelToSubtitleTrack() {
        if (mpv.subtitleTrack < 0)
            return
        for (var i = 0; i < subs.tracks.length; ++i) {
            var t = subs.tracks[i]
            if (!t.browsable)
                continue
            if (mpv.subtitleTrackMatches(mpv.subtitleTrack, t.streamIndex,
                                         t.sidecar ? t.sourcePath : "")) {
                trackTabs.currentIndex = i
                return
            }
        }
    }

    Connections {
        target: mpv
        // The extractor and mpv open the file independently, so the panel can
        // pick a tab before mpv has a track list to match it against. Retrying
        // when the list changes covers that, and also re-selects after a
        // sub-add lands.
        function onTracksChanged() { root.applySubtitleSelection() }
        function onSubtitleTrackChanged() { root.syncPanelToSubtitleTrack() }
    }

    function tracksOfType(type) {
        var out = []
        for (var i = 0; i < mpv.tracks.length; ++i) {
            if (mpv.tracks[i].type === type)
                out.push(mpv.tracks[i])
        }
        return out
    }

    function trackLabel(t) {
        var parts = []
        if (t.language !== undefined && t.language !== "")
            parts.push(t.language)
        if (t.title !== undefined && t.title !== "")
            parts.push(t.title)
        if (parts.length === 0)
            parts.push("track " + t.id)
        var label = parts.join(" · ")
        if (t.codec !== undefined && t.codec !== "")
            label += "  [" + t.codec + "]"
        if (t.external)
            label += "  (external)"
        return label
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

    // ---- keyboard ------------------------------------------------------
    // Every shortcut is gated on the search box not having focus: these are
    // plain letters and arrows, and a TextField sees key events first only for
    // keys it claims. Space and Left/Right it does claim; F and M it does not,
    // so without the guard typing "film" in the search box would toggle
    // fullscreen and mute.
    readonly property bool typing: searchField.activeFocus

    FileDialog {
        id: openDialog
        title: "Open media"
        nameFilters: ["Media files (*.mkv *.mp4 *.avi *.mov *.webm *.m4v *.ts *.flac *.mp3 *.opus)",
                      "All files (*)"]
        onAccepted: root.openFile(mpv.localFile(selectedFile))
    }

    Shortcut {
        sequences: ["Space", "K"]
        enabled: !root.typing
        onActivated: mpv.togglePause()
    }
    Shortcut {
        sequence: "Right"
        enabled: !root.typing
        onActivated: mpv.seekRelative(5)
    }
    Shortcut {
        sequence: "Left"
        enabled: !root.typing
        onActivated: mpv.seekRelative(-5)
    }
    Shortcut {
        sequence: "Shift+Right"
        enabled: !root.typing
        onActivated: mpv.seekRelative(1)
    }
    Shortcut {
        sequence: "Shift+Left"
        enabled: !root.typing
        onActivated: mpv.seekRelative(-1)
    }
    Shortcut {
        sequence: "L"
        enabled: !root.typing
        onActivated: mpv.seekRelative(10)
    }
    Shortcut {
        sequence: "J"
        enabled: !root.typing
        onActivated: mpv.seekRelative(-10)
    }
    Shortcut {
        sequence: "Up"
        enabled: !root.typing
        onActivated: mpv.setVolume(mpv.volume + 5)
    }
    Shortcut {
        sequence: "Down"
        enabled: !root.typing
        onActivated: mpv.setVolume(mpv.volume - 5)
    }
    Shortcut {
        sequence: "M"
        enabled: !root.typing
        onActivated: mpv.toggleMute()
    }
    Shortcut {
        sequences: ["F", "F11"]
        enabled: !root.typing
        onActivated: root.toggleFullscreen()
    }
    Shortcut {
        // Only leaves fullscreen -- Escape in the search box clears it instead,
        // which is handled on the field itself.
        sequence: "Escape"
        enabled: root.fullscreen && !root.typing
        onActivated: root.visibility = Window.Windowed
    }
    Shortcut {
        sequence: "Ctrl+O"
        onActivated: openDialog.open()
    }
    Shortcut {
        sequence: "]"
        enabled: !root.typing
        onActivated: mpv.setSpeed(mpv.speed + 0.25)
    }
    Shortcut {
        sequence: "["
        enabled: !root.typing
        onActivated: mpv.setSpeed(mpv.speed - 0.25)
    }
    Shortcut {
        sequence: "Backspace"
        enabled: !root.typing
        onActivated: mpv.setSpeed(1.0)
    }
    Shortcut {
        // Tab rather than a letter: the panel is worth toggling while fullscreen
        // and every letter near it is already spoken for.
        sequence: "Tab"
        enabled: !root.typing
        onActivated: root.panelVisible = !root.panelVisible
    }
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: searchField.forceActiveFocus()
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

    // Drag and drop, over the whole window rather than just the video: dropping
    // on the subtitle panel plainly means "open this" too.
    DropArea {
        anchors.fill: parent
        onEntered: (drag) => {
            // Only take it if it is actually a file. Refusing here is what stops
            // the cursor promising a drop that would do nothing.
            drag.accepted = drag.hasUrls && drag.urls.length > 0
        }
        onDropped: (drop) => {
            if (!drop.hasUrls || drop.urls.length === 0)
                return
            // Extra files in one drop are ignored rather than queued: there is
            // no playlist yet, and silently playing the last one would be worse.
            root.openFile(mpv.localFile(drop.urls[0]))
            drop.acceptProposedAction()
        }
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
                            root.openFile(initialFile)
                    }
                    onLogMessage: (text) => console.log("[mpv]", text)
                }

                // Double-click for fullscreen, and any movement wakes the chrome
                // back up while fullscreen. Deliberately no click-to-pause: the
                // window has to be clicked to focus it under WSLg, and pausing
                // on that is infuriating.
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onPositionChanged: chromeTimer.restart()
                    onDoubleClicked: root.toggleFullscreen()
                    cursorShape: root.showChrome ? Qt.ArrowCursor : Qt.BlankCursor
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
                        text: "no media loaded\ndrop a file here, or press Ctrl+O"
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 52
                color: "#16161d"
                visible: root.showChrome

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    Button {
                        text: mpv.paused ? "Play" : "Pause"
                        enabled: mpv.duration > 0
                        onClicked: mpv.togglePause()
                    }

                    // One label rather than two flanking the slider: the transport
                    // is crowded enough that the seek bar was being squeezed to
                    // its minimum in a default-sized window.
                    Label {
                        color: "#9a9aa8"
                        font.pixelSize: 11
                        text: root.fmt(mpv.position) + " / " + root.fmt(mpv.duration)
                    }

                    Slider {
                        id: seekBar
                        Layout.fillWidth: true
                        // The transport carries enough fixed-width controls that
                        // fillWidth alone leaves the seek bar unusably narrow in
                        // a small window; it is the one control that must stay
                        // draggable, so the rest can overflow instead.
                        Layout.minimumWidth: 150
                        from: 0
                        to: Math.max(mpv.duration, 0.1)
                        enabled: mpv.duration > 0
                        value: pressed ? value : mpv.position
                        onMoved: mpv.seek(value)
                    }

                    // mpv's own track selection. The panel's tabs cover browsable
                    // text tracks; these menus also reach what it cannot list --
                    // audio, and bitmap subtitles, which carry pictures rather
                    // than text and so can be displayed but never browsed.
                    Button {
                        text: "Audio"
                        font.pixelSize: 11
                        padding: 6
                        enabled: mpv.duration > 0
                        onClicked: audioMenu.popup(0, -audioMenu.height)
                        ToolTip.visible: hovered
                        ToolTip.text: "Audio track"

                        Menu {
                            id: audioMenu

                            Repeater {
                                model: root.tracksOfType("audio")
                                MenuItem {
                                    required property var modelData
                                    text: root.trackLabel(modelData)
                                    checkable: true
                                    checked: modelData.id === mpv.audioTrack
                                    onTriggered: mpv.setAudioTrack(modelData.id)
                                }
                            }

                            MenuItem {
                                text: "Off"
                                checkable: true
                                checked: mpv.audioTrack < 0
                                onTriggered: mpv.setAudioTrack(-1)
                            }
                        }
                    }

                    // Words, not glyphs: WSL images ship no emoji font, so a
                    // speaker icon renders as a tofu box. Also doubles as the
                    // mute readout -- the slider alone cannot show that 60% is
                    // muted.
                    Button {
                        text: mpv.muted ? "Muted" : "Vol"
                        font.pixelSize: 11
                        padding: 6
                        flat: true
                        onClicked: mpv.toggleMute()
                        ToolTip.visible: hovered
                        ToolTip.text: mpv.muted ? "Muted — click or M to unmute"
                                                : Math.round(mpv.volume) + "% (M to mute)"
                    }

                    Slider {
                        id: volumeSlider
                        Layout.preferredWidth: 80
                        from: 0
                        to: 100
                        // Follows mpv rather than owning the value, so the
                        // keyboard and the slider cannot disagree.
                        value: mpv.muted ? 0 : mpv.volume
                        onMoved: {
                            if (mpv.muted)
                                mpv.toggleMute()
                            mpv.setVolume(value)
                        }
                    }

                    Button {
                        text: mpv.speed.toFixed(2).replace(/0$/, "") + "×"
                        font.pixelSize: 11
                        padding: 6
                        onClicked: speedMenu.popup(0, -speedMenu.height)
                        ToolTip.visible: hovered
                        ToolTip.text: "Playback speed ([ and ], Backspace resets)"

                        Menu {
                            id: speedMenu

                            Repeater {
                                model: [0.5, 0.75, 1.0, 1.25, 1.5, 2.0]
                                MenuItem {
                                    required property var modelData
                                    text: modelData + "×"
                                    checkable: true
                                    // Float comparison with a tolerance: [ and ]
                                    // step by 0.25 and land on these values but
                                    // not necessarily on the same bit pattern.
                                    checked: Math.abs(mpv.speed - modelData) < 0.01
                                    onTriggered: mpv.setSpeed(modelData)
                                }
                            }
                        }
                    }

                    Button {
                        text: "Subs"
                        font.pixelSize: 11
                        padding: 6
                        enabled: mpv.duration > 0
                        onClicked: subMenu.popup(0, -subMenu.height)
                        ToolTip.visible: hovered
                        ToolTip.text: "Subtitle track burned over the video"

                        Menu {
                            id: subMenu

                            Repeater {
                                model: root.tracksOfType("sub")
                                MenuItem {
                                    required property var modelData
                                    text: root.trackLabel(modelData)
                                    checkable: true
                                    checked: modelData.id === mpv.subtitleTrack
                                    onTriggered: mpv.setSubtitleTrack(modelData.id)
                                }
                            }

                            MenuItem {
                                text: "Off"
                                checkable: true
                                checked: mpv.subtitleTrack < 0
                                onTriggered: mpv.setSubtitleTrack(-1)
                            }
                        }
                    }

                    Button {
                        text: root.fullscreen ? "Exit" : "Full"
                        font.pixelSize: 11
                        padding: 6
                        flat: true
                        onClicked: root.toggleFullscreen()
                        ToolTip.visible: hovered
                        ToolTip.text: root.fullscreen ? "Leave fullscreen (Esc)"
                                                      : "Fullscreen (F)"
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
            visible: root.panelVisible

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
                        if (track !== undefined && track.browsable) {
                            root.currentTrack = track.id
                            root.applySubtitleSelection()
                        }
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
