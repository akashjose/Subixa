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
                    panelUi.tabIndex = i
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

    // View state the panel must not lose when it moves between windows, since
    // the panel item is destroyed and rebuilt each time it does.
    QtObject {
        id: panelUi
        property string searchText: ""
        property int tabIndex: 0
        property bool following: true
    }

    property bool panelDetached: false

    // One definition, instantiated by whichever Loader is active -- docked in the
    // SplitView, or filling the detached window.
    Component {
        id: panelComponent

        SubtitlePanel {
            manager: subs
            linesModel: lines
            ui: panelUi
            currentRow: root.currentRow
            detached: root.panelDetached
            onSeekRequested: (seconds) => mpv.seek(seconds)
            onTrackActivated: (index) => root.selectTrack(index)
            onDetachToggled: root.panelDetached = !root.panelDetached
        }
    }

    // The browser in its own window: fullscreen video on one screen and the
    // whole track on another is the arrangement this feature exists for.
    Window {
        id: panelWindow
        width: 460
        height: 820
        minimumWidth: 260
        minimumHeight: 300
        title: "Subtitles — custom media player"
        color: "#12121a"
        visible: root.panelDetached
        // Closing the window is the same statement as pressing Dock.
        onClosing: root.panelDetached = false

        Loader {
            id: detachedPanel
            anchors.fill: parent
            sourceComponent: root.panelDetached ? panelComponent : null
        }
    }

    // Whichever copy is live. Null for the instant between the two Loaders
    // swapping, so every use has to tolerate that.
    readonly property Item activePanel: root.panelDetached ? detachedPanel.item
                                                           : dockedPanel.item

    function selectTrack(index) {
        var track = subs.tracks[index]
        if (track === undefined || !track.browsable)
            return
        root.currentTrack = track.id
        root.applySubtitleSelection()
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
        var track = subs.tracks[panelUi.tabIndex]
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
                panelUi.tabIndex = i
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
    //
    // Positioning is the ListView's job rather than ours: setting currentIndex
    // and letting highlightRangeMode keep it inside a band means the line being
    // spoken sits in the upper middle of the panel with the next few visible
    // below it. The old positionViewAtIndex(Contain) scrolled the *minimum*
    // distance instead, which pinned each new line to the bottom edge and showed
    // no lookahead at all.
    // The panel positions itself from currentRow; this only has to compute it.
    function syncFollow() {
        var row = lines.rowAt(Math.round(mpv.position * 1000))
        if (row !== root.currentRow)
            root.currentRow = row
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
    readonly property bool typing: root.activePanel !== null
                                   && root.activePanel.searchActive

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
        onActivated: {
            if (!root.panelVisible)
                root.panelVisible = true
            if (root.activePanel !== null)
                root.activePanel.focusSearch()
        }
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: !root.typing
        onActivated: root.panelDetached = !root.panelDetached
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

    // SplitView rather than a fixed 340 px pane, so the browser can be widened
    // for long lines or narrowed to give the picture room. The handle's position
    // is the only thing it adds over the old RowLayout.
    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        // The default handle is a hairline: hard to hit with a mouse and
        // invisible against a dark theme. This one is wide enough to grab and
        // lights up under the cursor so it reads as draggable.
        handle: Rectangle {
            implicitWidth: 6
            color: SplitHandle.pressed ? "#42618f"
                                       : (SplitHandle.hovered ? "#2c2c3a" : "#1b1b25")

            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: 28
                color: "#4a4a5c"
                visible: !parent.SplitHandle.pressed
            }
        }

        // ---- video + transport ----------------------------------------
        ColumnLayout {
            SplitView.fillWidth: true
            // The transport bar drops controls as it narrows, so this only has
            // to leave room for Play, the clock and a usable seek bar.
            SplitView.minimumWidth: 380
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
                    id: transport
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    // Controls drop out as the bar narrows rather than clipping
                    // off the right-hand end, which is what happened once the
                    // panel became resizable. Play, the clock and the seek bar
                    // always stay; the rest go in reverse order of how often
                    // they are reached for, and each has a keyboard shortcut, so
                    // nothing becomes unreachable when its button is hidden.
                    readonly property bool showSpeed: width > 700
                    readonly property bool showVolumeSlider: width > 620
                    readonly property bool showMute: width > 560
                    readonly property bool showFullscreen: width > 520
                    readonly property bool showTracks: width > 440

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
                        visible: transport.showTracks
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
                        // Kept longer than the slider: muting is the thing you
                        // reach for in a hurry, and M is easy to forget.
                        visible: transport.showMute || mpv.muted
                        onClicked: mpv.toggleMute()
                        ToolTip.visible: hovered
                        ToolTip.text: mpv.muted ? "Muted — click or M to unmute"
                                                : Math.round(mpv.volume) + "% (M to mute)"
                    }

                    Slider {
                        id: volumeSlider
                        visible: transport.showVolumeSlider
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
                        visible: transport.showSpeed || Math.abs(mpv.speed - 1.0) > 0.01
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
                        visible: transport.showTracks
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
                        visible: transport.showFullscreen
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
        // A Loader rather than the panel itself: the same component is used
        // detached, and only one instance may exist at a time.
        Loader {
            id: dockedPanel
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 240
            SplitView.maximumWidth: 900
            visible: root.panelVisible && !root.panelDetached
            sourceComponent: visible ? panelComponent : null
        }
    }
}
