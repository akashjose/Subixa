import QtCore
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
    // The file, not just the application: with a queue that advances on its own,
    // a title bar that never changes is the player being coy about what it is
    // playing.
    title: root.currentFile === ""
           ? "custom media player"
           : root.fileLabel(root.currentFile) + " — custom media player"
    color: Theme.windowBackground

    // Everything the player should look the same way it did last time: window
    // and panel geometry, volume, and the theme. Written into the same QSettings
    // file PlaybackHistory uses, under its own group, so there is one file to
    // look at when something is remembered that should not be.
    //
    // Restored in Component.onCompleted and saved on close rather than bound in
    // both directions: a binding from a window's own width to a stored value is
    // broken by the first resize anyway, and this way a fullscreen session never
    // writes the screen's size back as the window size.
    Settings {
        id: prefs
        category: "ui"

        property int windowWidth: 1180
        property int windowHeight: 700
        property int windowX: -1
        property int windowY: -1
        property bool windowMaximized: false

        property int panelWidth: 340
        property bool panelVisible: true
        property bool panelDetached: false
        property int detachedX: -1
        property int detachedY: -1
        property int detachedWidth: 460
        property int detachedHeight: 820

        property real volume: 100
        property bool muted: false

        property bool darkTheme: true
        property int rowFontSize: 12
    }

    // The theme is a singleton so both windows follow it; the saved value has to
    // be pushed into it once at startup and written back when it changes.
    Connections {
        target: Theme
        function onDarkChanged() { prefs.darkTheme = Theme.dark }
        function onRowFontSizeChanged() { prefs.rowFontSize = Theme.rowFontSize }
    }

    Component.onCompleted: {
        Theme.dark = prefs.darkTheme
        Theme.rowFontSize = prefs.rowFontSize

        root.width = prefs.windowWidth
        root.height = prefs.windowHeight
        // -1 means "never saved": let the window manager place it rather than
        // dropping it at the top-left corner.
        if (prefs.windowX >= 0 && prefs.windowY >= 0) {
            root.x = prefs.windowX
            root.y = prefs.windowY
        }
        if (prefs.windowMaximized)
            root.visibility = Window.Maximized

        root.panelVisible = prefs.panelVisible
        root.panelDetached = prefs.panelDetached
        dockedPanel.SplitView.preferredWidth = prefs.panelWidth
    }

    function savePreferences() {
        // Only a windowed geometry is worth keeping: saving while maximised or
        // fullscreen would store the screen and reopen edge to edge forever.
        if (root.visibility === Window.Windowed) {
            prefs.windowWidth = root.width
            prefs.windowHeight = root.height
            prefs.windowX = root.x
            prefs.windowY = root.y
        }
        prefs.windowMaximized = root.visibility === Window.Maximized
        prefs.panelVisible = root.panelVisible
        prefs.panelDetached = root.panelDetached
        prefs.panelWidth = dockedPanel.SplitView.preferredWidth
        if (root.panelDetached) {
            prefs.detachedX = panelWindow.x
            prefs.detachedY = panelWindow.y
            prefs.detachedWidth = panelWindow.width
            prefs.detachedHeight = panelWindow.height
        }
    }

    // Parsed subtitle tracks. Independent of mpv by necessity -- see
    // SubtitleExtractor.
    SubtitleManager {
        id: subs
        // Named for the headless QML harness, which waits on loaded() and reads
        // the view state back. Nothing in the app looks these up.
        objectName: "subtitleManager"
        onLoaded: {
            root.currentTrack = -1
            var index = root.preferredTrackIndex()
            if (index >= 0) {
                root.currentTrack = subs.tracks[index].id
                panelUi.tabIndex = index
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
        objectName: "playbackHistory"
    }

    // What plays after this file. Opening anything makes its folder the queue,
    // so the next episode follows without anyone building a playlist; dropping
    // several files makes exactly those the queue instead.
    Playlist {
        id: playlist
        objectName: "playlist"
    }

    function playNext() {
        var path = playlist.next()
        if (path !== "")
            root.openFile(path)
    }

    function playPrevious() {
        var path = playlist.previous()
        if (path !== "")
            root.openFile(path)
    }

    Connections {
        target: mpv
        // Reaching the end moves to the next file, and stops at the last one --
        // keep-open leaves the final frame up, which is a better ending than a
        // black window. A file that failed to load never reports a duration, so
        // this cannot turn a broken file into a run through the whole folder.
        function onEndOfFile() {
            if (mpv.duration <= 0 || !playlist.hasNext)
                return
            root.playNext()
            // keep-open paused playback at the end of the outgoing file, and
            // pause is a player property rather than a per-file one -- without
            // this the next file arrives already paused, having never been
            // asked. A manual skip is left alone: someone who paused meant it.
            mpv.setPaused(false)
        }
    }

    // View state the panel must not lose when it moves between windows, since
    // the panel item is destroyed and rebuilt each time it does.
    QtObject {
        id: panelUi
        objectName: "panelUi"
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
            onExportRequested: root.exportCurrentTrack()
        }
    }

    // The browser in its own window: fullscreen video on one screen and the
    // whole track on another is the arrangement this feature exists for.
    Window {
        id: panelWindow
        width: prefs.detachedWidth
        height: prefs.detachedHeight
        minimumWidth: 260
        minimumHeight: 300
        title: "Subtitles — custom media player"
        color: Theme.panelBackground
        visible: root.panelDetached
        // Closing the window is the same statement as pressing Dock.
        onClosing: {
            root.savePreferences()
            root.panelDetached = false
        }

        Component.onCompleted: {
            if (prefs.detachedX >= 0 && prefs.detachedY >= 0) {
                panelWindow.x = prefs.detachedX
                panelWindow.y = prefs.detachedY
            }
        }

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
        // A tab click is a statement about this film, so it is worth keeping.
        // Only explicit choices are remembered -- storing what the sync handlers
        // do would overwrite the user's track with mpv's default on every open.
        history.rememberSubtitle(root.currentFile, track.streamIndex,
                                 track.sidecar ? track.sourcePath : "",
                                 track.language)
    }

    // Which tab a newly parsed file should open on: the track last read in this
    // very file, else the language last chosen anywhere, else the first
    // browsable track. Before this, every open reset to mpv's default -- on a
    // film with 65 tracks that means hunting for the right one every time.
    function preferredTrackIndex() {
        var i, t
        var stored = history.subtitleFor(root.currentFile)

        if (stored.streamIndex !== undefined) {
            for (i = 0; i < subs.tracks.length; ++i) {
                t = subs.tracks[i]
                if (!t.browsable)
                    continue
                // Sidecars are matched by path and embedded tracks by ffmpeg
                // stream index, the same split MpvObject uses: neither numbering
                // follows from the other.
                var match = stored.sidecarPath !== ""
                          ? (t.sidecar && t.sourcePath === stored.sidecarPath)
                          : (!t.sidecar && t.streamIndex === stored.streamIndex)
                if (match)
                    return i
            }
        }

        var lang = history.preferredLanguage()
        if (lang !== "") {
            for (i = 0; i < subs.tracks.length; ++i) {
                t = subs.tracks[i]
                if (t.browsable && t.language === lang)
                    return i
            }
        }

        for (i = 0; i < subs.tracks.length; ++i) {
            if (subs.tracks[i].browsable)
                return i
        }
        return -1
    }

    // The transport's Subs menu reaches tracks the panel cannot list, so a
    // choice made there has to be remembered too. mpv describes a track by
    // ff-index and, for a sidecar, by the filename it loaded.
    function rememberMpvSubtitle(track) {
        if (root.currentFile === "" || track.id === undefined)
            return
        var sidecar = track.external && track.externalFilename !== undefined
                    ? track.externalFilename : ""
        history.rememberSubtitle(root.currentFile,
                                 sidecar === "" ? track.ffIndex : -1, sidecar,
                                 track.language !== undefined ? track.language : "")
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

        // A file already in the queue keeps it -- that is a playlist advance, or
        // someone reopening a file they dropped. Anything else starts a new
        // queue from its own folder, which is what makes "next" work without
        // ever building a playlist by hand.
        if (playlist.contains(path))
            playlist.setCurrentPath(path)
        else
            playlist.openFolderOf(path)

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

    onClosing: {
        root.rememberPosition()
        root.savePreferences()
    }

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
        // From the playlist, so the dialog and the folder scan cannot disagree
        // about what counts as media.
        nameFilters: playlist.dialogNameFilters()
        onAccepted: root.openFile(mpv.localFile(selectedFile))
    }

    // ---- notices -------------------------------------------------------
    // A file that will not play used to leave a black picture and a line in a
    // log nobody is reading. Both failures that can follow an open -- mpv's and
    // the extractor's -- land here, and so does the one thing worth confirming
    // rather than reporting: an export that worked.
    property string noticeText: ""
    property bool noticeIsError: true

    function reportError(text) {
        root.noticeText = text
        root.noticeIsError = true
        noticeTimer.restart()
    }

    function reportDone(text) {
        root.noticeText = text
        root.noticeIsError = false
        noticeTimer.restart()
    }

    Timer {
        id: noticeTimer
        // Long enough to read a sentence, short enough that it is gone by the
        // time the next file is open. Dismissable either way.
        interval: 9000
        onTriggered: root.noticeText = ""
    }

    Connections {
        target: mpv
        function onPlaybackFailed(reason) {
            root.reportError(root.currentFile === ""
                             ? "Playback failed: " + reason
                             : root.fileLabel(root.currentFile) + ": " + reason)
        }
    }

    Connections {
        target: subs
        // Subtitles failing is not the same as playback failing: the film may be
        // playing perfectly with an unreadable sidecar next to it, so this says
        // which half broke.
        function onFailed(reason) { root.reportError("Subtitles: " + reason) }
    }

    // ---- export --------------------------------------------------------
    FileDialog {
        id: exportDialog
        title: "Export subtitle track"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: ["SubRip subtitles (*.srt)", "All files (*)"]
        // selectedFile puts the dialog in the right folder but leaves the name
        // field empty: the file being named does not exist yet, and nothing in
        // the listing matches it. currentFile is the one the open dialog
        // actually shows in that field, and it only sticks once the dialog is
        // up. (visibleChanged, not opened -- a FileDialog is a native-capable
        // dialog rather than a Popup, and has no opened signal.)
        onVisibleChanged: {
            if (visible)
                exportDialog.currentFile = root.pendingExportUrl
        }
        onAccepted: {
            var failure = subs.exportTrack(root.currentTrack, selectedFile)
            if (failure !== "")
                root.reportError("Export failed: " + failure)
            else
                root.reportDone("Exported " + root.fileLabel(mpv.localFile(selectedFile)))
        }
    }

    // What the export dialog should come up on. A property rather than a local,
    // because the dialog re-applies it when it opens as well.
    property url pendingExportUrl

    function exportCurrentTrack() {
        if (root.currentTrack < 0)
            return
        // Opens next to the film, named after it and the track's language --
        // which is the file anyone exporting a track was going to type anyway.
        root.pendingExportUrl = subs.suggestedExportUrl(root.currentTrack,
                                                        root.currentFile)
        // The folder follows from the file: setting selectedFile is what puts
        // the dialog next to the film, which it does reliably even when the
        // name field does not take.
        exportDialog.selectedFile = root.pendingExportUrl
        exportDialog.open()
    }

    function fileLabel(path) {
        var cut = path.lastIndexOf("/")
        return cut >= 0 ? path.substring(cut + 1) : path
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
        // mpv's own bindings for this, and the only pair of keys near the
        // transport that nothing else has claimed.
        sequence: ">"
        enabled: !root.typing
        onActivated: root.playNext()
    }
    Shortcut {
        sequence: "<"
        enabled: !root.typing
        onActivated: root.playPrevious()
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
    Shortcut {
        // Both spellings: Ctrl+= is what the key is actually labelled, Ctrl++
        // is what people expect it to be called.
        sequences: ["Ctrl+=", "Ctrl++"]
        onActivated: Theme.rowFontSize = Math.min(Theme.maximumRowFontSize,
                                                  Theme.rowFontSize + 1)
    }
    Shortcut {
        sequence: "Ctrl+-"
        onActivated: Theme.rowFontSize = Math.max(Theme.minimumRowFontSize,
                                                  Theme.rowFontSize - 1)
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
            if (drop.urls.length === 1) {
                root.openFile(mpv.localFile(drop.urls[0]))
            } else {
                // Several files are a queue in the order they were dropped, not
                // one file and a shrug. The order is kept rather than sorted:
                // dropping them in a particular order means that order.
                var paths = []
                for (var i = 0; i < drop.urls.length; ++i)
                    paths.push(mpv.localFile(drop.urls[i]))
                playlist.setFiles(paths)
                root.openFile(playlist.currentPath)
            }
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
            color: SplitHandle.pressed
                   ? Theme.accent
                   : (SplitHandle.hovered ? Theme.splitHandleHover : Theme.splitHandle)

            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: 28
                color: Theme.splitGrip
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
                color: Theme.videoBackground

                MpvObject {
                    id: mpv
                    anchors.fill: parent

                    Component.onCompleted: {
                        // Before the file, so nothing is ever played at the
                        // wrong volume for the first fraction of a second.
                        mpv.setVolume(prefs.volume)
                        if (prefs.muted)
                            mpv.toggleMute()
                        if (initialFile !== "")
                            root.openFile(initialFile)
                    }
                    // Written straight through rather than saved on close: a
                    // kill -9 should not be able to lose the volume, and the
                    // Settings type coalesces the writes a slider drag produces.
                    onVolumeChanged: prefs.volume = mpv.volume
                    onMutedChanged: prefs.muted = mpv.muted
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

                // Over the picture rather than in the transport bar: a file that
                // will not play is the one thing that must not be missed, and
                // the bar is hidden entirely in fullscreen.
                Rectangle {
                    anchors.top: parent.top
                    anchors.topMargin: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(parent.width - 24, noticeRow.implicitWidth + 28)
                    height: noticeRow.implicitHeight + 18
                    radius: 6
                    visible: root.noticeText !== ""
                    // Same banner for both, because both are "something just
                    // happened that you did not watch for" -- but an export that
                    // worked must not be dressed up as a failure.
                    color: root.noticeIsError ? Theme.errorBackground
                                              : Theme.overlayBackground
                    border.color: root.noticeIsError ? Theme.errorBorder
                                                     : Theme.overlayBorder

                    RowLayout {
                        id: noticeRow
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            color: root.noticeIsError ? Theme.errorText
                                                      : Theme.overlayText
                            wrapMode: Text.WordWrap
                            text: root.noticeText
                        }

                        Button {
                            // A multiplication sign, not an emoji cross: WSL
                            // images ship no emoji font and it would be a box.
                            text: "×"
                            flat: true
                            padding: 2
                            onClicked: root.noticeText = ""
                        }
                    }
                }

                // Proves QML composites over the video surface -- the whole
                // reason for the render API rather than --wid.
                Rectangle {
                    anchors.centerIn: parent
                    visible: mpv.duration <= 0
                    width: hint.implicitWidth + 32
                    height: hint.implicitHeight + 20
                    radius: 6
                    color: Theme.overlayBackground
                    border.color: Theme.overlayBorder

                    Label {
                        id: hint
                        anchors.centerIn: parent
                        color: Theme.overlayText
                        text: "no media loaded\ndrop a file here, or press Ctrl+O"
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 52
                color: Theme.transportBackground
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
                    // Kept longest of the optional controls: with a queue
                    // loaded these are the buttons being reached for.
                    readonly property bool showQueue: width > 400 && playlist.count > 1

                    Button {
                        text: "‹"
                        font.pixelSize: 13
                        padding: 6
                        flat: true
                        visible: transport.showQueue
                        enabled: playlist.hasPrevious
                        onClicked: root.playPrevious()
                        ToolTip.visible: hovered
                        ToolTip.text: "Previous file in the folder (<)"
                    }

                    Button {
                        text: mpv.paused ? "Play" : "Pause"
                        enabled: mpv.duration > 0
                        onClicked: mpv.togglePause()
                    }

                    Button {
                        text: "›"
                        font.pixelSize: 13
                        padding: 6
                        flat: true
                        visible: transport.showQueue
                        enabled: playlist.hasNext
                        onClicked: root.playNext()
                        ToolTip.visible: hovered
                        ToolTip.text: "Next file in the folder (>)"
                    }

                    // Says there is a queue at all, and where in it you are.
                    // Without it, auto-advancing to another file looks like the
                    // player wandering off on its own.
                    Label {
                        visible: transport.showQueue
                        color: Theme.textDim
                        font.pixelSize: 11
                        text: (playlist.currentIndex + 1) + "/" + playlist.count
                        ToolTip.visible: queueHover.hovered
                        ToolTip.text: root.fileLabel(root.currentFile)

                        HoverHandler { id: queueHover }
                    }

                    // One label rather than two flanking the slider: the transport
                    // is crowded enough that the seek bar was being squeezed to
                    // its minimum in a default-sized window.
                    Label {
                        color: Theme.textMuted
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
                                    onTriggered: {
                                        mpv.setSubtitleTrack(modelData.id)
                                        root.rememberMpvSubtitle(modelData)
                                    }
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
