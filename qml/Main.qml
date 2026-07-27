// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Subixa

ApplicationWindow {
    id: root
    // Wide enough that the video keeps a usable pane with the browser docked at
    // its default 360, which is the arrangement this player is for.
    readonly property int defaultWindowWidth: 1980
    readonly property int defaultWindowHeight: 1200
    readonly property int defaultPanelWidth: 360
    width: root.defaultWindowWidth
    height: root.defaultWindowHeight
    minimumWidth: 680
    minimumHeight: 420
    visible: true
    // The file, not just the application: with a queue that advances on its own,
    // a title bar that never changes is the player being coy about what it is
    // playing.
    title: root.currentFile === ""
           ? "Subixa"
           : root.fileLabel(root.currentFile) + " — Subixa"
    color: Theme.color.bgBase

    // Playback lives in C++ and is created in main() before the QML engine, so
    // it outlives the video item and its render context. See MpvEngine.
    readonly property var mpv: mpvEngine

    // Every service is also reachable as a property of root, and every binding
    // that hands one to a child goes through these rather than naming the id
    // directly.
    //
    // This is the shadowing trap that already cost this codebase the `lines` ->
    // `linesModel` rename in SubtitlePanel, and it bit again the moment the
    // settings window appeared: a child declaring `required property var prefs`
    // and being given `prefs: prefs` resolves the right-hand side to its *own*
    // property, so the binding is to itself. Nothing errors -- the property is a
    // `var`, so it simply holds undefined, every control reading through it
    // falls back to its declared default, and the settings window comes up
    // showing zeroes for values that are not zero.
    //
    // Qualifying with `root.` makes that unrepresentable rather than something
    // to remember.
    readonly property var linesModel: lines
    readonly property var queue: playlist
    readonly property var prefsStore: prefs
    readonly property var subStyleStore: subStyle
    readonly property var shortcutStore: keys
    readonly property var subtitleManager: subs

    // ---- persisted preferences -----------------------------------------
    // Everything the player should look the same way it did last time. Written
    // into the same QSettings file PlaybackHistory and ShortcutRegistry use,
    // under its own group, so there is one file to look at when something is
    // remembered that should not be.
    //
    // Restored in Component.onCompleted and saved on close rather than bound in
    // both directions: a binding from a window's own width to a stored value is
    // broken by the first resize anyway, and this way a fullscreen session never
    // writes the screen's size back as the window size.
    Settings {
        id: prefs
        category: "ui"

        property int windowWidth: 1980
        property int windowHeight: 1200
        // Off, and a stored geometry stops shadowing the default -- which is the
        // confusing half of remembering it: raising the default does nothing for
        // anyone who has ever moved the window.
        property bool rememberGeometry: true
        property int windowX: -1
        property int windowY: -1
        property bool windowMaximized: false

        property int panelWidth: 360
        property bool panelVisible: true
        property bool panelDetached: false
        property int detachedX: -1
        property int detachedY: -1
        property int detachedWidth: 460
        property int detachedHeight: 820

        property real volume: 100
        property bool muted: false

        property bool darkTheme: true
        property int rowFontSize: 13
        property bool showStyling: true
        property bool reducedMotion: false
        // "auto" | "gpu" | "painted". Auto trusts the driver check.
        property string textRendering: "auto"

        // Playback behaviour
        property bool playNextAutomatically: true
        // Minimising a film should not go on playing into a window nobody can
        // see. Music should: minimising is how an album gets out of the way.
        // The rule is the same either way -- stop what cannot be watched -- and
        // hasVideo is what tells the two apart.
        property bool pauseOnMinimize: true
        // Off, so coming back does not start playing on its own. Restoring a
        // window is often how someone goes looking for something rather than a
        // decision to watch, and a player that begins the moment it reappears is
        // the more startling of the two defaults. Pressing play is one key.
        property bool resumeOnRestore: false
        property int seekStep: 5
        property int seekStepLarge: 10
        property int volumeStep: 5
        property real speedStep: 0.25
        property int cueLoopLeadInMs: 200
        property int cueLoopTailMs: 300

        // Browser behaviour
        property bool showEndTime: false
        property bool showCueDuration: false
        property bool followOffOnScroll: true
    }

    // Subtitle appearance, applied to mpv rather than to the browser. Its own
    // group because it is about the picture, not about the window.
    //
    // The colours here are the only ones in the app that are not Theme tokens,
    // and deliberately so: they are how subtitles are drawn *over the film*, in
    // mpv's #AARRGGBB spelling. White on a black outline is right over any
    // footage and has nothing to do with whether the UI is light or dark --
    // tying them to the theme would mean switching to the light theme changed
    // the subtitles burnt into the picture.
    Settings {
        id: subStyle
        category: "subtitleStyle"

        property string font: "sans-serif"
        property int fontSize: 55
        property bool bold: false
        property bool italic: false
        property string color: "#ffffffff"
        property string borderColor: "#ff000000"
        property real borderSize: 3.0
        property real shadowOffset: 0.0
        property string backColor: "#00000000"
        property int pos: 100
        property real scale: 1.0
        property string assOverride: "yes"
    }

    // Pushes everything above into mpv. Called at startup and whenever a
    // control in the settings window moves.
    function applySubtitleStyle() {
        mpv.setSubtitleOption("sub-font", subStyle.font)
        mpv.setSubtitleOption("sub-font-size", String(subStyle.fontSize))
        mpv.setSubtitleOption("sub-bold", subStyle.bold ? "yes" : "no")
        mpv.setSubtitleOption("sub-italic", subStyle.italic ? "yes" : "no")
        mpv.setSubtitleOption("sub-color", subStyle.color)
        mpv.setSubtitleOption("sub-border-color", subStyle.borderColor)
        mpv.setSubtitleOption("sub-border-size", String(subStyle.borderSize))
        mpv.setSubtitleOption("sub-shadow-offset", String(subStyle.shadowOffset))
        mpv.setSubtitleOption("sub-back-color", subStyle.backColor)
        mpv.setSubtitleOption("sub-pos", String(subStyle.pos))
        mpv.setSubtitleOption("sub-scale", String(subStyle.scale))
        mpv.setSubtitleOption("sub-ass-override", subStyle.assOverride)
    }

    // The theme is a singleton so both windows follow it; the saved value has to
    // be pushed into it once at startup and written back when it changes.
    Connections {
        target: Theme
        function onDarkChanged() { prefs.darkTheme = Theme.dark }
        function onRowFontSizeChanged() { prefs.rowFontSize = Theme.rowFontSize }
        function onShowStylingChanged() { prefs.showStyling = Theme.showStyling }
        function onReducedMotionChanged() { prefs.reducedMotion = Theme.reducedMotion }
    }

    Component.onCompleted: {
        Theme.dark = prefs.darkTheme
        Theme.rowFontSize = prefs.rowFontSize
        Theme.showStyling = prefs.showStyling
        Theme.reducedMotion = prefs.reducedMotion
        root.applyTextRendering()

        root.width = prefs.windowWidth
        root.height = prefs.windowHeight
        // -1 means "never saved": let the window manager place it rather than
        // dropping it at the top-left corner.
        if (prefs.rememberGeometry && prefs.windowX >= 0 && prefs.windowY >= 0) {
            root.x = prefs.windowX
            root.y = prefs.windowY
        }
        if (prefs.windowMaximized)
            root.visibility = Window.Maximized

        root.panelVisible = prefs.panelVisible
        root.panelDetached = prefs.panelDetached
        dockedPanel.SplitView.preferredWidth = prefs.panelWidth
    }

    // A shadow costs a render pass, and on Mesa's software rasterizers that is
    // a real cost -- see traps 9 and 10. The engine reports what GL turned out
    // to be and the UI steps down rather than guessing.
    Connections {
        target: mpv
        function onSoftwareRenderingChanged() {
            Theme.effectsEnabled = !mpv.softwareRendering
        }
        // The renderer is only known once the video item has made a context, so
        // this arrives a moment after startup rather than in onCompleted.
        function onRendererNameChanged() { root.applyTextRendering() }
    }

    // Which text path to draw with. Auto asks the engine, which reports what
    // GL_RENDERER actually said rather than guessing from the platform.
    function applyTextRendering() {
        Theme.paintedText = prefs.textRendering === "painted"
                          || (prefs.textRendering === "auto"
                              && mpv.glyphRenderingSuspect)
        if (Theme.paintedText) {
            console.log("text: drawing with QPainter -- "
                        + mpv.rendererName
                        + " does not colour glyphs correctly")
        }
    }

    function resetWindowSize() {
        root.visibility = Window.Windowed
        root.width = root.defaultWindowWidth
        root.height = root.defaultWindowHeight
        dockedPanel.SplitView.preferredWidth = root.defaultPanelWidth
        prefs.windowWidth = root.defaultWindowWidth
        prefs.windowHeight = root.defaultWindowHeight
        prefs.panelWidth = root.defaultPanelWidth
        // -1 is "never saved", so the window manager places it rather than the
        // stored corner.
        prefs.windowX = -1
        prefs.windowY = -1
    }

    function savePreferences() {
        // Only a windowed geometry is worth keeping: saving while maximised or
        // fullscreen would store the screen and reopen edge to edge forever.
        if (root.visibility === Window.Windowed && prefs.rememberGeometry) {
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

    // ---- services -------------------------------------------------------
    // Parsed subtitle tracks. Independent of mpv by necessity -- see
    // SubtitleExtractor.
    SubtitleManager {
        id: subs
        // Named for the headless QML harness, which waits on loaded() and reads
        // the view state back. Nothing in the app looks these up.
        objectName: "subtitleManager"
        // Cue colours are chosen to sit over a picture; against a light panel
        // half of them would be invisible. The models adjust for this, and this
        // is how they learn what they are being drawn on.
        rowBackground: Theme.color.bgSurface
        onLoaded: {
            root.currentTrack = -1
            var index = root.preferredTrackIndex()
            if (index >= 0) {
                root.currentTrack = subs.tracks[index].id
                panelUi.tabIndex = index
            }
        }
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

    ShortcutRegistry {
        id: keys
        objectName: "shortcuts"
    }

    property int currentTrack: -1
    // View row of the cue playing right now, or -1 when playback is before the
    // first cue or that cue is filtered out.
    property int currentRow: -1

    readonly property bool fullscreen: visibility === Window.FullScreen

    // ---- minimising -------------------------------------------------------
    // A film playing into a taskbar button is nobody watching it, so minimising
    // stops it. Music is the opposite: minimising is *how* an album is put on in
    // the background, and pausing it there would be the player second-guessing
    // the obvious. mpv.hasVideo is what separates them, and it discounts cover
    // art so a tagged mp3 does not read as a film.
    readonly property bool minimized: visibility === Window.Minimized
    // Whether the pause was ours to undo. Restoring must not start something
    // that was already paused when it was minimised.
    property bool pausedByMinimize: false
    onMinimizedChanged: {
        if (root.minimized) {
            if (prefs.pauseOnMinimize && mpv.hasVideo && !mpv.paused) {
                mpv.setPaused(true)
                root.pausedByMinimize = true
            }
        } else if (root.pausedByMinimize) {
            // Cleared either way: the pause stops being ours the moment the
            // window is back, so a later manual pause is never undone by a
            // minimise that happened before it.
            root.pausedByMinimize = false
            if (prefs.resumeOnRestore)
                mpv.setPaused(false)
        }
    }

    property bool panelVisible: true
    // What the panel was doing before fullscreen hid it, so leaving fullscreen
    // restores that rather than unconditionally showing it.
    property bool panelVisibleBeforeFullscreen: true
    onFullscreenChanged: {
        if (root.fullscreen) {
            root.panelVisibleBeforeFullscreen = root.panelVisible
            root.panelVisible = false
        } else {
            root.panelVisible = root.panelVisibleBeforeFullscreen
        }
    }

    // Chrome floats over the picture in fullscreen and fades; in a window it is
    // a normal layout child and stays put, so the picture never jumps by the
    // height of the transport bar.
    readonly property bool showChrome: !fullscreen || chromeTimer.running
                                       || transportHover.hovered

    Timer {
        id: chromeTimer
        interval: 2500
    }

    // ---- file lifecycle -------------------------------------------------
    // The file currently open, tracked because mpv's own path is not what the
    // history is keyed on and the outgoing file has to be saved before switching.
    property string currentFile: ""
    // Seconds to jump to once mpv reports the file loaded, or -1.
    property double pendingResume: -1

    // One way in for every source of a file: the command line, the dialog, and
    // a drop.
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

    function openFiles(paths) {
        if (!paths || paths.length === 0)
            return
        if (paths.length === 1) {
            root.openFile(paths[0])
            return
        }
        // Several files are a queue in the order they were given, not one file
        // and a shrug. The order is kept rather than sorted: someone who names
        // three files in a particular order meant that order.
        playlist.setFiles(paths)
        root.openFile(playlist.currentPath)
    }

    function rememberPosition() {
        if (root.currentFile !== "" && mpv.duration > 0)
            history.remember(root.currentFile, mpv.position, mpv.duration)
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
        // Seeking has to wait for the file to be loaded; mpv has no position to
        // seek within before that.
        function onFileLoaded() {
            if (root.pendingResume > 0) {
                mpv.seek(root.pendingResume)
                root.pendingResume = -1
            }
        }

        // Reaching the end moves to the next file, and stops at the last one --
        // keep-open leaves the final frame up, which is a better ending than a
        // black window. A file that failed to load never reports a duration, so
        // this cannot turn a broken file into a run through the whole folder.
        function onEndOfFile() {
            if (!prefs.playNextAutomatically || mpv.duration <= 0 || !playlist.hasNext)
                return
            root.playNext()
            // keep-open paused playback at the end of the outgoing file, and
            // pause is a player property rather than a per-file one -- without
            // this the next file arrives already paused, having never been
            // asked. A manual skip is left alone: someone who paused meant it.
            mpv.setPaused(false)
        }

        function onPlaybackFailed(reason) {
            root.notify(root.currentFile === ""
                        ? "Playback failed: " + reason
                        : root.fileLabel(root.currentFile) + ": " + reason,
                        "error", playlist.hasNext ? "Skip to next" : "")
        }

        function onCommandFailed(what, reason) {
            // mpv refusing a command used to be silent, which is how a malformed
            // sidecar produced a tab that simply did nothing.
            root.notify(what + ": " + reason, "error")
        }

        function onScreenshotSaved(path) {
            root.notify("Screenshot saved to " + path, "success")
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
        // Without this the detached panel is still a visible top-level window,
        // so quitOnLastWindowClosed never fires and the process survives as an
        // orphan subtitle browser with no video and no way back.
        Qt.quit()
    }

    function toggleFullscreen() {
        visibility = root.fullscreen ? Window.Windowed : Window.FullScreen
    }

    function fileLabel(path) {
        var cut = path.lastIndexOf("/")
        return cut >= 0 ? path.substring(cut + 1) : path
    }

    function fmt(t) {
        if (!isFinite(t) || t < 0)
            return "--:--"
        var s = Math.floor(t % 60)
        var m = Math.floor(t / 60) % 60
        var h = Math.floor(t / 3600)
        var two = function (n) { return (n < 10 ? "0" : "") + n }
        // Hours padded once the file is long enough to have them, so the string
        // does not change width as it ticks past an hour.
        return (h > 0 ? two(h) + ":" + two(m) : m) + ":" + two(s)
    }

    // ---- track reconciliation ------------------------------------------
    // Rows of the selected track, filtered by the search box. Switching tracks
    // just repoints the proxy at another model -- no rows are copied, which is
    // the whole reason the QVariantList snapshot is gone.
    SubtitleFilterModel {
        id: lines
        sourceModel: subs.tracks.length > 0 ? subs.model(root.currentTrack) : null
        // The browser's own timestamps move with the subtitle delay, so the
        // panel and the picture cannot disagree about when a line is spoken.
        delayMs: Math.round(mpv.subtitleDelay * 1000)
    }

    // View state the panel must not lose when it moves between windows, since
    // the panel item is destroyed and rebuilt each time it does.
    QtObject {
        id: panelUi
        objectName: "panelUi"
        property string searchText: ""
        property int tabIndex: 0
        property bool following: true
        property bool syncBarVisible: false
    }

    property bool panelDetached: false

    function selectTrack(index) {
        var tracks = subs.tracks
        var track = tracks[index]
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
        // Hoisted. Every read of subs.tracks converts a QVariantList of
        // QVariantMaps into a fresh JS array of JS objects, and this used to do
        // it once per loop iteration -- about 4 200 conversions on the 65-track
        // film, for one answer.
        var tracks = subs.tracks
        var i, t
        var stored = history.subtitleFor(root.currentFile)

        if (stored.streamIndex !== undefined) {
            for (i = 0; i < tracks.length; ++i) {
                t = tracks[i]
                if (!t.browsable)
                    continue
                // Sidecars are matched by path and embedded tracks by ffmpeg
                // stream index, the same split MpvEngine uses: neither numbering
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
            for (i = 0; i < tracks.length; ++i) {
                t = tracks[i]
                if (t.browsable && t.language === lang)
                    return i
            }
        }

        for (i = 0; i < tracks.length; ++i) {
            if (tracks[i].browsable)
                return i
        }
        return -1
    }

    // The transport's subtitle menu reaches tracks the panel cannot list, so a
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

    // Tells mpv to burn the track the panel is showing over the video. Until this
    // existed the two were independent, so you could read one language in the
    // panel while another rendered on screen with no way to reconcile them.
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
        var tracks = subs.tracks
        for (var i = 0; i < tracks.length; ++i) {
            var t = tracks[i]
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
        var all = mpv.tracks
        var out = []
        for (var i = 0; i < all.length; ++i) {
            if (all[i].type === type)
                out.push(all[i])
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
            parts.push("Track " + t.id)
        var label = parts.join(" · ")
        if (t.external)
            label += "  (external)"
        return label
    }

    // Auto-follow. Cheap enough to run on every position tick: rowAt() is a
    // binary search plus a proxy row mapping, and the view is only touched when
    // the row actually changes.
    function syncFollow() {
        var row = lines.rowAt(Math.round(mpv.position * 1000))
        if (row !== root.currentRow)
            root.currentRow = row
    }

    Connections {
        target: mpv
        function onPositionChanged() { root.syncFollow() }
        function onSubtitleDelayChanged() { root.syncFollow() }
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

    // ---- notices --------------------------------------------------------
    // A file that will not play used to leave a black picture and a line in a
    // log nobody is reading.
    property string noticeText: ""
    property string noticeSeverity: "info"
    property string noticeAction: ""

    function notify(text, severity, actionText) {
        root.noticeText = text
        root.noticeSeverity = severity === undefined ? "info" : severity
        root.noticeAction = actionText === undefined ? "" : actionText
        noticeTimer.restart()
    }

    Timer {
        id: noticeTimer
        // Long enough to read a sentence, short enough that it is gone by the
        // time the next file is open. Paused while the banner is hovered, so it
        // cannot vanish out from under someone reading it.
        interval: 9000
        running: false
        onTriggered: {
            if (noticeBanner.hovered)
                noticeTimer.restart()
            else
                root.noticeText = ""
        }
    }

    Connections {
        target: subs
        // Subtitles failing is not the same as playback failing: the film may be
        // playing perfectly with an unreadable sidecar next to it, so this says
        // which half broke.
        function onFailed(reason) {
            root.notify("Subtitles: " + reason, "error", "Load a subtitle file…")
        }
    }

    // ---- on-screen display ----------------------------------------------
    // Volume, speed and subtitle-delay changes need to be visible in fullscreen,
    // where the transport bar is hidden and the settings window is not an
    // option.
    property string osdText: ""
    function osd(text) {
        root.osdText = text
        osdTimer.restart()
    }
    Timer { id: osdTimer; interval: 1100; onTriggered: root.osdText = "" }

    // ---- actions ---------------------------------------------------------
    // Every keyboard action in one switch, driven by the registry rather than by
    // 125 lines of hardcoded Shortcut elements. The menus and tooltips read
    // their key caps from the same table, so a remapped binding reaches them
    // without anything being written out twice.
    function dispatch(id) {
        switch (id) {
        case "play-pause": case "play-pause-alt": mpv.togglePause(); break
        case "stop": mpv.stop(); root.currentFile = ""; subs.clear(); break
        case "speed-up":
            mpv.setSpeed(mpv.speed + prefs.speedStep)
            root.osd(mpv.speed.toFixed(2) + "×"); break
        case "speed-down":
            mpv.setSpeed(mpv.speed - prefs.speedStep)
            root.osd(mpv.speed.toFixed(2) + "×"); break
        case "speed-reset": mpv.setSpeed(1.0); root.osd("1.00×"); break
        case "next-file": root.playNext(); break
        case "previous-file": root.playPrevious(); break

        case "seek-forward": mpv.seekRelative(prefs.seekStep); break
        case "seek-back": mpv.seekRelative(-prefs.seekStep); break
        case "seek-forward-1": mpv.seekRelative(1); break
        case "seek-back-1": mpv.seekRelative(-1); break
        case "seek-forward-10": mpv.seekRelative(prefs.seekStepLarge); break
        case "seek-back-10": mpv.seekRelative(-prefs.seekStepLarge); break
        case "cue-next": root.seekToAdjacentCue(1); break
        case "cue-previous": root.seekToAdjacentCue(-1); break
        case "chapter-next": root.stepChapter(1); break
        case "chapter-previous": root.stepChapter(-1); break

        case "volume-up":
            mpv.setVolume(mpv.volume + prefs.volumeStep)
            root.osd(Math.round(mpv.volume) + "%"); break
        case "volume-down":
            mpv.setVolume(mpv.volume - prefs.volumeStep)
            root.osd(Math.round(mpv.volume) + "%"); break
        case "mute":
            mpv.toggleMute()
            root.osd(mpv.muted ? "Muted" : Math.round(mpv.volume) + "%"); break
        case "audio-delay-up": root.nudgeAudioDelay(0.05); break
        case "audio-delay-down": root.nudgeAudioDelay(-0.05); break

        case "subtitle-toggle":
            mpv.setSubtitleVisible(!mpv.subtitleVisible)
            root.osd(mpv.subtitleVisible ? "Subtitles on" : "Subtitles off"); break
        case "sub-delay-up": root.nudgeSubtitleDelay(0.05); break
        case "sub-delay-down": root.nudgeSubtitleDelay(-0.05); break
        case "sub-delay-reset":
            mpv.setSubtitleDelay(0); root.osd("Subtitle delay 0.000 s"); break
        case "sub-sync-here": root.syncSubtitlesToCurrentRow(); break

        case "panel-toggle": root.panelVisible = !root.panelVisible; break
        case "panel-detach": root.panelDetached = !root.panelDetached; break
        case "search-focus":
            if (!root.panelVisible)
                root.panelVisible = true
            if (root.activePanel !== null)
                root.activePanel.focusSearch()
            break
        case "follow-toggle": panelUi.following = !panelUi.following; break
        case "row-larger":
            Theme.rowFontSize = Math.min(Theme.maximumRowFontSize, Theme.rowFontSize + 1)
            break
        case "row-smaller":
            Theme.rowFontSize = Math.max(Theme.minimumRowFontSize, Theme.rowFontSize - 1)
            break
        case "copy-cue": root.copyCurrentCue(false); break
        case "copy-cue-timed": root.copyCurrentCue(true); break
        case "export-track": root.exportCurrentTrack(); break

        case "loop-set-a":
            mpv.setLoopStart(mpv.position)
            root.osd("Loop start " + root.fmt(mpv.position)); break
        case "loop-set-b":
            mpv.setLoopEnd(mpv.position)
            root.osd("Loop end " + root.fmt(mpv.position)); break
        case "loop-clear": mpv.clearLoop(); root.osd("Loop cleared"); break
        case "loop-this-cue": root.loopCurrentCue(); break

        case "fullscreen": case "fullscreen-alt": root.toggleFullscreen(); break
        case "leave-fullscreen":
            // Esc means "get this out of the way", and what is in the way
            // depends on where it is: fullscreen comes down to a window, and a
            // window goes to the taskbar. Minimised rather than closed, because
            // one key must never be able to end a viewing.
            if (root.fullscreen)
                root.visibility = Window.Windowed
            else
                root.showMinimized()
            break

        case "open-file": openDialog.open(); break
        case "open-subtitle": subtitleDialog.open(); break
        case "screenshot": mpv.screenshot(true); break
        case "settings": settingsLoader.active = true; break
        case "quit": root.close(); break
        }
    }

    // Every shortcut is gated on the search box not having focus unless the
    // registry says the binding carries a modifier a TextField does not claim.
    // Without that guard, typing "film" into the search box would toggle
    // fullscreen and mute.
    readonly property bool typing: root.activePanel !== null
                                   && root.activePanel.searchActive

    Repeater {
        model: keys.model()

        delegate: Item {
            required property var modelData

            Shortcut {
                sequence: modelData.sequence
                // Escape needs no gate of its own any more: it does something in
                // either window state now, and the search box is already covered
                // because leave-fullscreen does not work while typing, so the
                // field keeps the key and clears its own text.
                enabled: modelData.sequence !== ""
                         && (!root.typing || modelData.worksWhileTyping)
                onActivated: root.dispatch(modelData.id)
            }
        }
    }

    // ---- subtitle-reader actions ------------------------------------------
    function seekToAdjacentCue(direction) {
        var here = Math.round(mpv.position * 1000)
        var row = direction > 0 ? lines.rowAfter(here) : lines.rowBefore(here)
        if (row < 0)
            return
        // startMsAt already carries the subtitle delay, so this lands where the
        // line is actually spoken rather than where it is stored.
        mpv.seek(lines.startMsAt(row) / 1000)
    }

    function stepChapter(direction) {
        var chapters = mpv.chapters
        if (chapters.length === 0) {
            // No chapters is not an error, but silence looks like a broken key.
            root.osd("No chapters in this file")
            return
        }
        var now = mpv.position
        if (direction > 0) {
            for (var i = 0; i < chapters.length; ++i) {
                if (chapters[i].time > now + 0.5) {
                    mpv.seek(chapters[i].time)
                    root.osd(chapters[i].title)
                    return
                }
            }
        } else {
            for (var j = chapters.length - 1; j >= 0; --j) {
                if (chapters[j].time < now - 1.5) {
                    mpv.seek(chapters[j].time)
                    root.osd(chapters[j].title)
                    return
                }
            }
        }
    }

    function nudgeSubtitleDelay(delta) {
        mpv.adjustSubtitleDelay(delta)
        panelUi.syncBarVisible = true
        root.osd("Subtitle delay " + (mpv.subtitleDelay >= 0 ? "+" : "")
                 + mpv.subtitleDelay.toFixed(3) + " s")
    }

    function nudgeAudioDelay(delta) {
        mpv.setAudioDelay(mpv.audioDelay + delta)
        root.osd("Audio delay " + (mpv.audioDelay >= 0 ? "+" : "")
                 + mpv.audioDelay.toFixed(3) + " s")
    }

    // The resync workflow: pick a line, scrub to where it should actually be
    // spoken, and press the key. The delay is the difference.
    property int syncReferenceRow: -1
    function markSyncReference(row) {
        root.syncReferenceRow = row
        panelUi.syncBarVisible = true
        root.notify("Sync reference set. Move to where this line should be "
                    + "spoken, then press "
                    + keys.sequenceFor("sub-sync-here") + ".", "info")
    }

    function syncSubtitlesToCurrentRow() {
        var row = root.syncReferenceRow >= 0 ? root.syncReferenceRow : root.currentRow
        if (row < 0) {
            root.notify("Pick a line in the browser first.", "info")
            return
        }
        // cueStartMsAt is the *stored* time; the delay that puts it here is the
        // difference between that and where playback is now.
        var cueStart = lines.cueStartMsAt(row)
        if (cueStart < 0)
            return
        mpv.setSubtitleDelay(mpv.position - cueStart / 1000)
        root.syncReferenceRow = -1
        panelUi.syncBarVisible = true
        root.osd("Subtitle delay " + (mpv.subtitleDelay >= 0 ? "+" : "")
                 + mpv.subtitleDelay.toFixed(3) + " s")
    }

    function loopCurrentCue() {
        if (root.currentRow < 0) {
            root.osd("No line playing")
            return
        }
        var start = lines.startMsAt(root.currentRow)
        // Both already carry the subtitle delay. A cue with no usable end time
        // gets three seconds, which is about a spoken line.
        var end = lines.endMsAt(root.currentRow)
        if (end <= start)
            end = start + 3000
        mpv.setLoop((start - prefs.cueLoopLeadInMs) / 1000,
                    (end + prefs.cueLoopTailMs) / 1000)
        mpv.seek((start - prefs.cueLoopLeadInMs) / 1000)
        root.osd("Looping this line")
    }

    function copyCurrentCue(withTimestamp) {
        var row = root.currentRow
        if (row < 0) {
            root.osd("No line playing")
            return
        }
        root.copyRow(row, withTimestamp)
    }

    function copyRow(row, withTimestamp) {
        if (root.activePanel === null)
            return
        root.activePanel.copyRow(row, withTimestamp)
    }

    // ---- dialogs ----------------------------------------------------------
    FileDialog {
        id: openDialog
        title: "Open media"
        // From the playlist, so the dialog and the folder scan cannot disagree
        // about what counts as media.
        nameFilters: playlist.dialogNameFilters()
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            var paths = []
            for (var i = 0; i < selectedFiles.length; ++i)
                paths.push(mpv.localFile(selectedFiles[i]))
            root.openFiles(paths)
        }
    }

    FileDialog {
        id: subtitleDialog
        title: "Open subtitle file"
        nameFilters: ["Subtitles (*.srt *.ass *.ssa *.vtt *.sub)", "All files (*)"]
        onAccepted: {
            var path = mpv.localFile(selectedFile)
            mpv.addSubtitleFile(path)
            // Re-parse so the browser picks the sidecar up as well as mpv.
            if (root.currentFile !== "")
                subs.load(root.currentFile)
        }
    }

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
                root.notify("Export failed: " + failure, "error")
            else
                root.notify("Exported " + root.fileLabel(mpv.localFile(selectedFile)),
                            "success")
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
        exportDialog.selectedFile = root.pendingExportUrl
        exportDialog.open()
    }

    Loader {
        id: settingsLoader
        active: false
        sourceComponent: SettingsWindow {
            player: root
            mpv: root.mpv
            prefs: root.prefsStore
            subStyle: root.subStyleStore
            shortcuts: root.shortcutStore
            onClosing: settingsLoader.active = false
            onSubtitleStyleChanged: root.applySubtitleStyle()
            onTextRenderingChanged: root.applyTextRendering()
        }
    }

    // ---- panel ------------------------------------------------------------
    // One definition, instantiated by whichever Loader is active -- docked in the
    // SplitView, or filling the detached window.
    Component {
        id: panelComponent

        SubtitlePanel {
            manager: root.subtitleManager
            linesModel: root.linesModel
            ui: panelUi
            shortcuts: root.shortcutStore
            currentRow: root.currentRow
            detached: root.panelDetached
            subtitleDelay: root.mpv.subtitleDelay
            showEndTime: root.prefsStore.showEndTime
            showCueDuration: root.prefsStore.showCueDuration
            followOffOnScroll: root.prefsStore.followOffOnScroll
            onSeekRequested: (seconds) => root.mpv.seek(seconds)
            onTrackActivated: (index) => root.selectTrack(index)
            onDetachToggled: root.panelDetached = !root.panelDetached
            onExportRequested: root.exportCurrentTrack()
            onSettingsRequested: settingsLoader.active = true
            onOpenSubtitleRequested: subtitleDialog.open()
            onDelayNudged: (delta) => root.nudgeSubtitleDelay(delta)
            onDelayReset: root.dispatch("sub-delay-reset")
            onSyncReferenceRequested: (row) => root.markSyncReference(row)
            onLoopRowRequested: (row) => root.loopRow(row)
            onNotify: (text, severity) => root.notify(text, severity)
        }
    }

    function loopRow(row) {
        var start = lines.startMsAt(row)
        if (start < 0)
            return
        var end = lines.endMsAt(row)
        if (end <= start)
            end = start + 3000
        mpv.setLoop((start - prefs.cueLoopLeadInMs) / 1000,
                    (end + prefs.cueLoopTailMs) / 1000)
        mpv.seek((start - prefs.cueLoopLeadInMs) / 1000)
        root.osd("Looping this line")
    }

    // The browser in its own window: fullscreen video on one screen and the
    // whole track on another is the arrangement this feature exists for.
    Window {
        id: panelWindow
        width: prefs.detachedWidth
        height: prefs.detachedHeight
        minimumWidth: 280
        minimumHeight: 320
        title: "Subtitles — Subixa"
        color: Theme.color.bgSurface
        visible: root.panelDetached && root.visible
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

    // ---- drag and drop -----------------------------------------------------
    DropArea {
        id: dropArea
        anchors.fill: parent
        onEntered: (drag) => {
            // Only take it if it is actually a file. Refusing here is what stops
            // the cursor promising a drop that would do nothing.
            drag.accepted = drag.hasUrls && drag.urls.length > 0
        }
        onDropped: (drop) => {
            if (!drop.hasUrls || drop.urls.length === 0)
                return
            var paths = []
            for (var i = 0; i < drop.urls.length; ++i)
                paths.push(mpv.localFile(drop.urls[i]))
            // A dropped folder is the media inside it. Expanded here rather than
            // inside openFiles, so a folder holding a single film still arrives
            // as one file to open rather than a queue of one.
            var expanded = playlist.expand(paths)
            if (expanded.length === 0) {
                // Saying so, because the alternative is a drop that looks like
                // it missed the window. A folder of subtitles and nothing else
                // is the case that gets here.
                root.notify("Nothing playable in what was dropped", "error")
                drop.acceptProposedAction()
                return
            }
            root.openFiles(expanded)
            drop.acceptProposedAction()
        }
    }

    // ---- layout ------------------------------------------------------------
    // SplitView rather than a fixed pane, so the browser can be widened for long
    // lines or narrowed to give the picture room.
    SplitView {
        id: split
        anchors.fill: parent
        orientation: Qt.Horizontal

        // The default handle is a hairline: hard to hit with a mouse and
        // invisible against a dark theme.
        handle: Rectangle {
            implicitWidth: 8
            color: "transparent"

            // The affordance that was missing: without a resize cursor the
            // handle reads as a border.
            HoverHandler { cursorShape: Qt.SplitHCursor }

            Rectangle {
                anchors.centerIn: parent
                width: parent.SplitHandle.pressed ? 2 : 1
                height: parent.height
                color: parent.SplitHandle.pressed ? Theme.color.accent
                     : parent.SplitHandle.hovered ? Theme.color.splitHandleHover
                                                  : Theme.color.splitHandle
                Behavior on color {
                    ColorAnimation { duration: Theme.motion.fast }
                }
            }

            Icon {
                anchors.centerIn: parent
                name: "grip-vertical"
                size: 14
                color: Theme.color.textTertiary
                opacity: parent.SplitHandle.hovered ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
            }
        }

        // ---- video + transport ----------------------------------------
        ColumnLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 400
            spacing: 0

            Rectangle {
                id: videoArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.videoBackground

                MpvVideoItem {
                    id: video
                    anchors.fill: parent
                    engine: root.mpv

                    Component.onCompleted: {
                        // Before the file, so nothing is ever played at the
                        // wrong volume for the first fraction of a second.
                        root.mpv.setVolume(prefs.volume)
                        if (prefs.muted)
                            root.mpv.setMuted(true)
                        root.applySubtitleStyle()
                        if (initialFiles.length > 0)
                            root.openFiles(initialFiles)
                    }
                }

                Connections {
                    target: root.mpv
                    // Written straight through rather than saved on close: a
                    // kill -9 should not be able to lose the volume, and the
                    // Settings type coalesces the writes a slider drag produces.
                    function onVolumeChanged() { prefs.volume = root.mpv.volume }
                    function onMutedChanged() { prefs.muted = root.mpv.muted }
                    function onLogMessage(text) { console.log("[mpv]", text) }
                }

                // Double-click for fullscreen, and any movement wakes the chrome
                // back up while fullscreen. Deliberately no click-to-pause: the
                // window has to be clicked to focus it under WSLg, and pausing
                // on that is infuriating.
                MouseArea {
                    id: videoMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    // Right-click as well, for the context menu. Left is still
                    // listed explicitly because naming acceptedButtons at all
                    // replaces the default rather than adding to it, and
                    // dropping LeftButton would take the double-click with it.
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPositionChanged: chromeTimer.restart()
                    onDoubleClicked: root.toggleFullscreen()
                    // The same list the transport's overflow button opens, at
                    // the cursor. Every other player puts it here, and reaching
                    // for a button at the bottom of the window to find it is the
                    // step that was worth removing.
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton)
                            transportBar.popupOverflowAt(videoMouse, mouse.x, mouse.y)
                    }
                    cursorShape: root.showChrome ? Qt.ArrowCursor : Qt.BlankCursor

                    // The wheel over the picture is volume, in the step the
                    // arrow keys use and with the same readout, so the two are
                    // one control reached two ways. Inside the MouseArea rather
                    // than beside it, so there is no question of which of the
                    // two sees the event first. The panel scrolls as it did:
                    // this is bounded by the picture.
                    WheelHandler {
                        onWheel: (event) => {
                            // A sideways scroll on a trackpad reports no
                            // vertical movement; without this it would read as
                            // a turn downwards and quietly lower the volume.
                            if (event.angleDelta.y === 0)
                                return
                            mpv.setVolume(mpv.volume + (event.angleDelta.y > 0
                                                        ? prefs.volumeStep
                                                        : -prefs.volumeStep))
                            root.osd(Math.round(mpv.volume) + "%")
                        }
                    }
                }

                // Nothing loaded. The first thing a new user sees, so it says
                // what to do rather than what is absent.
                EmptyState {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 48, 420)
                    visible: root.currentFile === "" && root.noticeText === ""
                    iconName: "film"
                    title: "Nothing playing"
                    body: "Open a file, or drop files and folders anywhere in "
                          + "this window. Drop several to build a queue."
                    actionText: "Open file…"
                    actionShortcut: keys.sequenceFor("open-file")
                    onActionTriggered: openDialog.open()
                }

                // Drop feedback. The DropArea used to accept a file with no
                // visual response at all -- the cursor was the only hint.
                Rectangle {
                    anchors.fill: parent
                    visible: dropArea.containsDrag
                    color: Qt.alpha(Theme.color.accent, 0.18)
                    border.width: 2
                    border.color: Theme.color.accent

                    EmptyState {
                        anchors.centerIn: parent
                        compact: true
                        iconName: "folder-open"
                        title: "Drop to play"
                        body: "Files or folders. Several become a queue, in the "
                              + "order dropped."
                    }
                }

                // Over the picture rather than in the transport bar: a file that
                // will not play is the one thing that must not be missed, and
                // the bar is hidden entirely in fullscreen.
                Banner {
                    id: noticeBanner
                    anchors.top: parent.top
                    anchors.topMargin: Theme.space.xl
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: root.noticeText !== ""
                    opacity: visible ? 1 : 0
                    severity: root.noticeSeverity
                    message: root.noticeText
                    actionText: root.noticeAction
                    onDismissed: root.noticeText = ""
                    onActionTriggered: {
                        if (root.noticeAction === "Skip to next")
                            root.playNext()
                        else if (root.noticeAction === "Load a subtitle file…")
                            subtitleDialog.open()
                        root.noticeText = ""
                    }

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.motion.base }
                    }
                }

                // Volume, speed, delay: things changed by key that must be
                // visible when the transport bar is not.
                Rectangle {
                    anchors.centerIn: parent
                    visible: root.osdText !== ""
                    width: osdLabel.implicitWidth + 2 * Theme.space.xxl
                    height: osdLabel.implicitHeight + 2 * Theme.space.lg
                    radius: Theme.radius.lg
                    color: Theme.color.scrimVideo

                    AppText {
                        id: osdLabel
                        anchors.centerIn: parent
                        text: root.osdText
                        textFormat: Text.PlainText
                        color: Theme.color.onVideo
                        font.family: Theme.type.sans
                        font.pixelSize: Theme.type.titleSize
                        font.weight: Theme.type.weightStrong
                    }
                }

                // Fullscreen chrome floats over the picture rather than taking
                // layout space, so the video never jumps as it comes and goes.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: transportBar.implicitHeight + 40
                    visible: root.fullscreen && root.showChrome
                    opacity: visible ? 1 : 0
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: Theme.color.scrimVideo }
                    }
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.motion.base }
                    }
                }
            }

            // The transport, reparented in fullscreen so it floats over the
            // picture instead of occupying layout height.
            Item {
                id: transportSlot
                Layout.fillWidth: true
                Layout.preferredHeight: root.fullscreen ? 0 : transportBar.implicitHeight
                visible: !root.fullscreen
            }
        }

        // ---- docked subtitle browser -----------------------------------
        // A Loader rather than the panel itself: the same component is used
        // detached, and only one instance may exist at a time.
        Loader {
            id: dockedPanel
            SplitView.preferredWidth: 360
            SplitView.minimumWidth: 240
            SplitView.maximumWidth: 720
            visible: root.panelVisible && !root.panelDetached
            sourceComponent: visible ? panelComponent : null
        }
    }

    // The transport bar itself, parented to whichever slot is live. In a window
    // it sits under the video; in fullscreen it floats over the bottom of it.
    TransportBar {
        id: transportBar
        parent: root.fullscreen ? videoArea : transportSlot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.fullscreen ? Theme.space.md : 0
        anchors.leftMargin: root.fullscreen ? Theme.space.lg : 0
        anchors.rightMargin: root.fullscreen ? Theme.space.lg : 0

        mpv: root.mpv
        lines: root.linesModel
        playlist: root.queue
        shortcuts: root.shortcutStore
        overVideo: root.fullscreen
        panelVisible: root.panelVisible
        fullscreen: root.fullscreen
        visible: root.showChrome
        opacity: visible ? 1 : 0

        Behavior on opacity { NumberAnimation { duration: Theme.motion.base } }

        HoverHandler { id: transportHover }

        onAction: (id) => root.dispatch(id)
        onSeekRequested: (seconds) => root.mpv.seek(seconds)
        onAudioTrackPicked: (id) => root.mpv.setAudioTrack(id)
        onSubtitleTrackPicked: (track) => {
            if (track === null) {
                root.mpv.setSubtitleTrack(-1)
            } else {
                root.mpv.setSubtitleTrack(track.id)
                root.rememberMpvSubtitle(track)
            }
        }
        onQueueEntryPicked: (index) => {
            playlist.setCurrentPath(playlist.files[index])
            root.openFile(playlist.currentPath)
        }
        audioTracks: root.tracksOfType("audio")
        subtitleTracks: root.tracksOfType("sub")
        trackLabeller: root.trackLabel
    }
}
