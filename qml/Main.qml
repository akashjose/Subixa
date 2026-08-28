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
    // One video pixel per screen pixel for a 1080p film, with the browser docked
    // beside it -- the arrangement this player is for. There is no default
    // window size: the window is only ever the picture plus the browser it is
    // sharing the width with and the transport bar under it.
    readonly property int defaultPictureWidth: 1920
    readonly property int defaultPictureHeight: 1080
    readonly property int defaultPanelWidth: 360
    readonly property int splitHandleWidth: 8
    width: root.defaultPictureWidth + root.defaultPanelWidth + root.splitHandleWidth
    height: root.defaultPictureHeight + transportBar.implicitHeight
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
    // directly (trap 19).
    //
    // A child declaring `required property var prefs` and given `prefs: prefs`
    // resolves the right-hand side to its *own* property, so the binding is to
    // itself. Nothing errors: the property is a `var`, so it holds undefined and
    // every control reading through it falls back to its declared default.
    // Qualifying with `root.` makes that unrepresentable.
    readonly property var linesModel: lines
    readonly property var queue: playlist
    readonly property var prefsStore: prefs
    readonly property var subStyleStore: subStyle
    readonly property var shortcutStore: keys
    readonly property var subtitleManager: subs
    readonly property var historyStore: history

    // ---- persisted preferences -----------------------------------------
    // Written into the same QSettings file PlaybackHistory and ShortcutRegistry
    // use, under its own group, so there is one file to look at.
    //
    // Restored in Component.onCompleted and saved on close rather than bound in
    // both directions: a binding from a window's own width to a stored value is
    // broken by the first resize anyway, and this way a fullscreen session never
    // writes the screen's size back as the window size.
    Settings {
        id: prefs
        category: "ui"

        // The window rather than the picture, because this is restored before
        // there is a layout to measure chrome against. The panel's own state is
        // restored alongside it, so the picture comes back the size it was.
        // -1 is "never saved", and then the default picture decides the size.
        property int windowWidth: -1
        property int windowHeight: -1
        // Off, and a stored geometry stops shadowing the default -- which is the
        // confusing half of remembering it: raising the default does nothing for
        // anyone who has ever moved the window.
        property bool rememberGeometry: true
        property int windowX: -1
        property int windowY: -1
        property bool windowMaximized: false
        // The picture the zoom keys return to, saved on purpose rather than
        // whatever the window happened to be last. -1 until somebody saves one,
        // and then the arrangement the player ships with stands in.
        property int savedPictureWidth: -1
        property int savedPictureHeight: -1
        property bool announceResize: true

        property int panelWidth: root.defaultPanelWidth
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
        property bool pauseOnMinimize: true
        // Off, so restoring a window does not start playing on its own.
        property bool resumeOnRestore: false
        // Independent on purpose: finishing a film clears its position and must
        // not also forget which track was being read, which is why
        // PlaybackHistory keeps them in separate groups. Off gates only the
        // restore -- the history keeps being written, so switching one back on
        // remembers the period it was off too.
        property bool resumeWhereLeftOff: true
        property bool rememberSubtitleTrack: true
        property bool rememberAudioTrack: true
        property int seekStep: 5
        property int seekStepLarge: 10
        property int volumeStep: 5
        property real speedStep: 0.25
        property int cueLoopLeadInMs: 200
        property int cueLoopTailMs: 300

        // Browser behaviour
        property bool showEndTime: false
        property bool showCueDuration: false
        // On by default, unlike the QC readouts above: who is speaking is
        // reading material, not tooling.
        property bool showActors: true
        property bool followOffOnScroll: true
    }

    // Subtitle appearance, applied to mpv rather than to the browser. Its own
    // group because it is about the picture, not about the window.
    //
    // The colours here are the only ones in the app that are not Theme tokens,
    // and deliberately so: they are how subtitles are drawn *over the film*, in
    // mpv's #AARRGGBB spelling. Tying them to the theme would mean switching to
    // the light theme changed the subtitles burnt into the picture.
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

        // All of it or none of it: restoring the stored size while the switch is
        // off left an old size reopening forever, which is the opposite of what
        // the switch promises.
        if (prefs.rememberGeometry && prefs.windowWidth > 0) {
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
        } else {
            // The default picture assumes room for a 1080p frame and a browser
            // beside it. A smaller screen gets a smaller picture rather than a
            // window whose transport bar is off the edge of it.
            var limit = root.screen
            var wanted = Qt.size(root.width, root.height)
            root.width = limit ? Math.min(wanted.width, limit.desktopAvailableWidth)
                               : wanted.width
            root.height = limit ? Math.min(wanted.height, limit.desktopAvailableHeight)
                                : wanted.height
        }

        root.panelVisible = prefs.panelVisible
        root.panelDetached = prefs.panelDetached
        dockedPanel.SplitView.preferredWidth = prefs.panelWidth

        // Everything above resizes the window, and none of it is worth
        // announcing. A frame is not enough to wait: the panel's width lands in
        // a later pass than the window's.
        settleTimer.start()
    }

    Timer {
        id: settleTimer
        interval: 600
        onTriggered: root.sizeSettled = true
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

    function resetPictureSize() {
        root.visibility = Window.Windowed
        dockedPanel.SplitView.preferredWidth = root.defaultPanelWidth
        root.resizeToPicture(root.defaultPictureWidth, root.defaultPictureHeight,
                             "default size")
        prefs.windowWidth = root.width
        prefs.windowHeight = root.height
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
        // The tab is the whole selection -- currentTrack is derived from it --
        // so there is nothing else to set here.
        onLoaded: root.applyPreferredSubtitleTrack()
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

    // The track being browsed, in the browser's own numbering -- an index into
    // the manager's track list, which is not mpv's track ids and never was.
    //
    // Derived rather than assigned: three paths choose a track (a tab, the
    // transport's subtitle menu, and the one remembered for this file), and any
    // that failed to write both this and the tab left the browser listing the
    // previous track's cues. panelUi.tabIndex is the one selection.
    readonly property int currentTrack: {
        var track = subs.tracks[panelUi.tabIndex]
        return track !== undefined && track.browsable ? track.id : -1
    }
    // View row of the cue playing right now, or -1 when playback is before the
    // first cue or that cue is filtered out.
    property int currentRow: -1

    readonly property bool fullscreen: visibility === Window.FullScreen

    // ---- minimising -------------------------------------------------------
    // Minimising stops a film and not an album -- for music it is *how* you put
    // one on in the background. mpv.hasVideo separates them, discounting cover
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
    // Assigned rather than put through setPanelShown: the window size is the
    // screen going in and the pre-fullscreen size coming out, so the picture is
    // already where it was and widening for the returning panel would overshoot.
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

    // Separate from showChrome: chrome floats over the film only in fullscreen,
    // but a pointer parked on the picture is in the way in a window too.
    readonly property bool showPointer: pointerTimer.running || transportHover.hovered

    Timer {
        id: pointerTimer
        // Slower than mpv's 1 s: this player is used with a pointer in hand,
        // scrubbing and clicking subtitle rows.
        interval: 2000
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
        // The stored position is looked up only when resuming is wanted; the
        // store itself keeps recording either way, so the toggle is a choice
        // about behaviour rather than about what is remembered.
        root.pendingResume =
            prefs.resumeWhereLeftOff ? history.resumeFor(path) : -1

        // A file already in the queue keeps it -- that is a playlist advance, or
        // someone reopening a file they dropped. Anything else starts a new
        // queue from its own folder, which is what makes "next" work without
        // ever building a playlist by hand.
        if (playlist.contains(path))
            playlist.setCurrentPath(path)
        else
            playlist.openFolderOf(path)

        // Both selections start over, or the outgoing film's answers apply to
        // the incoming one.
        root.subtitleSelectionIsOurs = false
        root.wantedSubtitleId = -2
        root.audioSelectionApplied = false

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
        // setFiles is where a queue is filtered, so several files that are none
        // of them media leave nothing behind and openFile would return without a
        // word. Saying so here, because opening something and having nothing
        // happen at all looks like the window missed it. Worded for all three
        // callers -- a drop, the file dialog and the command line -- rather than
        // for the drop alone. One bad file needs no guard: it goes to mpv, which
        // reports what it could not open.
        if (playlist.currentPath === "") {
            root.notify("Nothing playable in those files", "error")
            return
        }
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
            // Or a malformed sidecar produces a tab that silently does nothing.
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

    // Whether the lit tab is a *choice* -- this file's remembered track, or a
    // preference that matched -- or merely the tab the panel fell back to
    // because it has to be showing something. Only a choice may be pushed at
    // mpv; forcing a fallback would override the track the release flagged.
    property bool subtitleSelectionIsOurs: false

    // A subtitle pick made in mpv's own numbering rather than the browser's --
    // the cycle key and the transport menu, both of which name a track by sid.
    // -2 is "no such pick outstanding"; -1 is deliberately off.
    //
    // It exists because the two intents cannot share one resync path. mpv
    // publishes `track-list` before `sid` (they are observed in that order, and
    // it delivers them in it), so a cycle's track-list change arrives while
    // `panelUi.tabIndex` still names the *previous* track. applySubtitleSelection
    // would then re-select that tab and undo the cycle -- measured, not assumed:
    // every press set the new sid and had it reverted a few milliseconds later,
    // so the key moved the track exactly once and then appeared dead.
    property int wantedSubtitleId: -2

    // The per-file toggle empties the map rather than skipping the call: the
    // choice must still be made, or the tab inherits the previous film's index.
    function applyPreferredSubtitleTrack() {
        var choice = subs.preferredTrackChoice(
                         prefs.rememberSubtitleTrack
                             ? history.subtitleFor(root.currentFile) : ({}),
                         history.preferredTracks("subtitle"))
        panelUi.tabIndex = choice.index
        root.subtitleSelectionIsOurs = choice.matched
        root.wantedSubtitleId = -2
        if (choice.matched)
            root.applySubtitleSelection()
        else
            root.syncPanelToSubtitleTrack()
    }

    function selectTrack(index) {
        var tracks = subs.tracks
        var track = tracks[index]
        if (track === undefined || !track.browsable)
            return
        // The panel has already moved its own tab by the time it says this, but
        // setting it here as well is what makes the tab the selection rather
        // than a thing that happens to agree with one.
        panelUi.tabIndex = index
        root.subtitleSelectionIsOurs = true
        // The tab is the selection again, so the cycle's claim is released.
        root.wantedSubtitleId = -2
        root.applySubtitleSelection()
        // Picking a track means wanting to read it. sub-visibility survives the
        // file it was turned off in, so without this a tab picked after the
        // hide key selects a track that is never drawn.
        mpv.setSubtitleVisible(true)
        // A tab click is a statement about this film, so it is worth keeping.
        // Only explicit choices are remembered -- storing what the sync handlers
        // do would overwrite the user's track with mpv's default on every open.
        history.rememberSubtitle(root.currentFile, track.streamIndex,
                                 track.sidecar ? track.sourcePath : "",
                                 track.language, track.forced,
                                 track.hearingImpaired)
    }

    // ---- audio track -----------------------------------------------------
    // Whether this file's audio track has been decided. mpv republishes its
    // track list more than once per file -- adding a sidecar does it -- and
    // without this the preference would undo a track picked by hand ten minutes
    // in.
    property bool audioSelectionApplied: false

    function applyPreferredAudioTrack() {
        if (root.audioSelectionApplied || mpv.audioTracks.length === 0)
            return
        root.audioSelectionApplied = true

        var id = mpv.preferredAudioTrack(
                     prefs.rememberAudioTrack
                         ? history.audioFor(root.currentFile) : ({}),
                     history.preferredTracks("audio"))
        // -1 is "no opinion", and the file's own default is already playing.
        if (id >= 0 && id !== mpv.audioTrack)
            mpv.setAudioTrack(id)
    }

    // What to store for an audio track picked by hand. External audio is
    // skipped: its ff-index counts within its own file, so storing it would
    // name an unrelated embedded stream.
    function rememberAudioChoice(id) {
        if (root.currentFile === "" || id < 0)
            return
        var track = mpv.trackById(id)
        if (track.id === undefined || track.external)
            return
        // An untagged track has no `language` key, and handing QML's `undefined`
        // to a QString parameter stores the literal text "undefined".
        history.rememberAudio(root.currentFile, track.ffIndex,
                              track.language !== undefined ? track.language : "",
                              track.visualImpaired === true)
    }

    // ---- cycling ---------------------------------------------------------
    // Both of these record where they land, as deliberately as a menu pick.
    function cycleAudioTrack() {
        var id = mpv.nextTrackOfType("audio", mpv.audioTrack)
        if (id < 0) {
            root.osd("No audio tracks")
            return
        }
        mpv.setAudioTrack(id)
        root.rememberAudioChoice(id)
        root.osd("Audio: " + root.trackLabel(mpv.trackById(id)))
    }

    function cycleSubtitleTrack() {
        // Emptiness is asked of the list, not read off a -1 return: with off in
        // the rotation, -1 is a track the cycle can legitimately land on.
        if (mpv.subtitleTracks.length === 0) {
            root.osd("No subtitle tracks")
            return
        }

        var id = mpv.nextTrackOfType("sub", mpv.subtitleTrack, true)
        // The cycle names a track the way mpv does, so it owns the selection
        // until the browser or a preference takes it back.
        root.wantedSubtitleId = id
        root.subtitleSelectionIsOurs = true
        mpv.setSubtitleTrack(id)

        if (id < 0) {
            root.osd("Subtitles off")
            return
        }
        // Reaching a track by the cycle means wanting to see it, and
        // sub-visibility is one flag for the whole session: without this the
        // track changes under a hidden layer and the key looks dead.
        mpv.setSubtitleVisible(true)
        var track = mpv.trackById(id)
        root.rememberMpvSubtitle(track)
        root.osd("Subtitles: " + root.trackLabel(track))
    }

    // ---- window sizing ---------------------------------------------------
    // The picture is what every size here means: what the zoom keys set, what
    // the readout says, what the reset button resets, what a saved default
    // holds. The window is that plus the two spans below.
    readonly property int pictureWidth: Math.round(video.width)
    readonly property int pictureHeight: Math.round(video.height)

    // The browser's share of the window width. Computed from the panel rather
    // than measured, so it is already right in the pass that shows, hides or
    // resets it -- a measurement would still be describing the old layout.
    function dockedPanelSpan() {
        return root.panelVisible && !root.panelDetached
             ? dockedPanel.SplitView.preferredWidth + root.splitHandleWidth
             : 0
    }

    // The transport bar's share of the window height. Nothing here changes it,
    // so measuring is safe, and only a layout knows what it came to.
    function transportSpan() {
        return root.height - video.height
    }

    // One way out for every sizing action, so the readout knows this size came
    // from us rather than from a drag.
    function resizeToPicture(w, h, why) {
        var wanted = Qt.size(Math.round(w) + root.dockedPanelSpan(),
                             Math.round(h) + root.transportSpan())

        // 200% of a 4K film is larger than most screens, and a window bigger
        // than the screen is one whose transport bar cannot be reached.
        var limit = root.screen
        if (limit) {
            wanted = Qt.size(Math.min(wanted.width, limit.desktopAvailableWidth),
                             Math.min(wanted.height, limit.desktopAvailableHeight))
        }
        root.resizeReason = why
        root.width = wanted.width
        root.height = wanted.height
    }

    // 100% is one video pixel per screen pixel.
    function zoomTo(scale) {
        if (root.fullscreen) {
            root.osd("Leave fullscreen first")
            return
        }
        if (mpv.videoSize.width <= 0 || mpv.videoSize.height <= 0) {
            root.osd("No video to size against")
            return
        }
        root.resizeToPicture(mpv.videoSize.width * scale,
                             mpv.videoSize.height * scale,
                             Math.round(scale * 100) + "%")
    }

    function zoomToDefault() {
        if (root.fullscreen) {
            root.osd("Leave fullscreen first")
            return
        }
        // -1 is "never saved one", and then the picture the player ships with
        // stands in.
        root.resizeToPicture(prefs.savedPictureWidth > 0 ? prefs.savedPictureWidth
                                                         : root.defaultPictureWidth,
                             prefs.savedPictureHeight > 0 ? prefs.savedPictureHeight
                                                          : root.defaultPictureHeight,
                             "default size")
    }

    function saveDefaultPictureSize() {
        if (root.fullscreen) {
            root.osd("Leave fullscreen first")
            return
        }
        prefs.savedPictureWidth = root.pictureWidth
        prefs.savedPictureHeight = root.pictureHeight
        root.osd("Saved " + root.pictureWidth + " × " + root.pictureHeight
                 + " as your default")
    }

    // Showing or hiding the browser hands the window the width it released or
    // took, so the picture does not move. Without this a zoom preset stops being
    // true the moment the panel does, which is the whole reason the picture is
    // the anchor.
    function setPanelShown(visible, detached) {
        var before = root.dockedPanelSpan()
        root.panelVisible = visible
        root.panelDetached = detached
        var delta = root.dockedPanelSpan() - before
        if (delta === 0 || root.fullscreen || root.visibility !== Window.Windowed)
            return
        var limit = root.screen
        root.width = limit
                   ? Math.min(root.width + delta, limit.desktopAvailableWidth)
                   : root.width + delta
    }

    // The transport's subtitle menu reaches tracks the panel cannot list, so a
    // choice made there has to be remembered too. Which file it names is decided
    // here; how mpv's description of a track becomes what the store keeps is
    // decided in C++, because it is mpv's numbering being translated.
    function rememberMpvSubtitle(track) {
        if (root.currentFile === "" || track.id === undefined)
            return
        var entry = mpv.subtitleHistoryEntry(track)
        history.rememberSubtitle(root.currentFile, entry.streamIndex,
                                 entry.sidecarPath, entry.language, entry.forced,
                                 entry.hearingImpaired)
    }

    // Tells mpv to burn the track the panel is showing over the video, so the
    // two cannot end up on different languages.
    function applySubtitleSelection() {
        var track = subs.tracks[panelUi.tabIndex]
        if (track === undefined || !track.browsable)
            return
        if (track.sidecar)
            mpv.selectSubtitleFile(track.sourcePath)
        else
            mpv.selectSubtitleStream(track.streamIndex)
    }

    // The other direction: a track picked from the transport menu moves the
    // panel's tab, and currentTrack derives from that, so the list and the
    // export target follow without a second write.
    //
    // The search is in C++, where the two numberings meet: one file can be named
    // relatively in the extractor and absolutely by mpv. -1 means mpv is showing
    // something the browser cannot list, or has not caught up with the file yet,
    // and both mean leave the tab alone.
    function syncPanelToSubtitleTrack() {
        var index = mpv.browserTrackForSubtitle(subs.tracks)
        if (index >= 0)
            panelUi.tabIndex = index
    }

    Connections {
        target: mpv
        // The extractor and mpv open the file independently, so the panel can
        // pick a tab before mpv has a track list to match it against. Retrying
        // when the list changes covers that, and also re-selects after a
        // sub-add lands.
        function onTracksChanged() {
            // When the selection is not ours the arrow points the other way and
            // the panel follows mpv instead. A pick made in mpv's numbering
            // points that way too: mpv already holds the track, so there is
            // nothing to re-assert, and re-asserting the tab would undo it.
            if (root.subtitleSelectionIsOurs && root.wantedSubtitleId === -2)
                root.applySubtitleSelection()
            else
                root.syncPanelToSubtitleTrack()
            root.applyPreferredAudioTrack()
        }
        function onSubtitleTrackChanged() { root.syncPanelToSubtitleTrack() }
    }

    function trackLabel(t) {
        var parts = []
        if (t.language !== undefined && t.language !== "") {
            // The name, not mpv's two-letter code: this menu read "en · SDH"
            // while the browser's tab beside it read "ENG SDH" for one track.
            var named = subs.languageName(t.language)
            parts.push(named !== "" ? named : t.language)
        }
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
    // So a file that will not play says so, rather than leaving a black picture
    // and a line in a log.
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
        // Paused while the banner is hovered, so it cannot vanish out from
        // under someone reading it.
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

    // ---- the resize readout ----------------------------------------------
    // Appended to the readout when we are the ones resizing, and cleared once
    // said: the next resize is a drag until proven otherwise.
    property string resizeReason: ""
    // Or every launch opens with an OSD over the first frame.
    property bool sizeSettled: false
    property size announcedPicture: Qt.size(0, 0)

    function announceSize() {
        var why = root.resizeReason
        root.resizeReason = ""
        if (!root.sizeSettled || !prefs.announceResize || root.fullscreen)
            return
        // Showing or hiding the panel moves the window and leaves the picture
        // alone, so there is nothing to report -- and reporting it anyway would
        // put an OSD over every Tab press.
        if (why === "" && root.pictureWidth === root.announcedPicture.width
                       && root.pictureHeight === root.announcedPicture.height)
            return
        root.announcedPicture = Qt.size(root.pictureWidth, root.pictureHeight)
        root.osd(root.pictureWidth + " × " + root.pictureHeight
                 + (why === "" ? "" : "  ·  " + why))
    }

    // Debounced: a drag changes the size every frame, and a programmed resize
    // sets width and height separately, so announcing on the change itself
    // would report the half-applied size.
    Timer {
        id: resizeAnnounce
        interval: 120
        onTriggered: root.announceSize()
    }
    onWidthChanged: resizeAnnounce.restart()
    onHeightChanged: resizeAnnounce.restart()
    // The picture can also move without the window: a panel toggle the screen
    // has no room to absorb, or one while maximised.
    Connections {
        target: video
        function onWidthChanged() { resizeAnnounce.restart() }
        function onHeightChanged() { resizeAnnounce.restart() }
    }

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
        case "audio-cycle": root.cycleAudioTrack(); break

        case "subtitle-toggle":
            mpv.setSubtitleVisible(!mpv.subtitleVisible)
            root.osd(mpv.subtitleVisible ? "Subtitles on" : "Subtitles off"); break
        case "subtitle-cycle": root.cycleSubtitleTrack(); break
        case "sub-delay-up": root.nudgeSubtitleDelay(0.05); break
        case "sub-delay-down": root.nudgeSubtitleDelay(-0.05); break
        case "sub-delay-reset":
            mpv.setSubtitleDelay(0); root.osd("Subtitle delay 0.000 s"); break
        case "sub-sync-here": root.syncSubtitlesToCurrentRow(); break

        case "panel-toggle":
            root.setPanelShown(!root.panelVisible, root.panelDetached); break
        case "panel-detach":
            root.setPanelShown(root.panelVisible, !root.panelDetached); break
        case "search-focus":
            if (!root.panelVisible)
                root.setPanelShown(true, root.panelDetached)
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
        case "zoom-half": root.zoomTo(0.5); break
        case "zoom-one": root.zoomTo(1.0); break
        case "zoom-one-half": root.zoomTo(1.5); break
        case "zoom-double": root.zoomTo(2.0); break
        case "zoom-default": root.zoomToDefault(); break
        case "zoom-save": root.saveDefaultPictureSize(); break
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
        // The property, not the invokable: as a one-shot call this was read
        // once at startup, so a rebind did not reach the running shortcut until
        // the next launch.
        model: keys.model

        delegate: Item {
            required property var modelData

            Shortcut {
                sequence: modelData.sequence
                // Escape needs no gate of its own any more: it does something in
                // either window state now, and the search box is already covered
                // because leave-fullscreen does not work while typing, so the
                // field keeps the key and clears its own text.
                // `capturing` is the settings page recording a keystroke.
                // Without that gate, pressing Ctrl+S to rebind something takes
                // a screenshot and the capture never sees the key.
                enabled: modelData.sequence !== ""
                         && !keys.capturing
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
            history: root.historyStore
            manager: root.subtitleManager
            qtVersion: qtRuntimeVersion
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
            showActors: root.prefsStore.showActors
            followOffOnScroll: root.prefsStore.followOffOnScroll
            onSeekRequested: (seconds) => root.mpv.seek(seconds)
            onTrackActivated: (index) => root.selectTrack(index)
            onDetachToggled: root.setPanelShown(root.panelVisible,
                                                !root.panelDetached)
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
            root.setPanelShown(root.panelVisible, false)
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
            implicitWidth: root.splitHandleWidth
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
                    onPositionChanged: {
                        chromeTimer.restart()
                        pointerTimer.restart()
                    }
                    onDoubleClicked: root.toggleFullscreen()
                    // The same list the transport's overflow button opens, at
                    // the cursor. Every other player puts it here, and reaching
                    // for a button at the bottom of the window to find it is the
                    // step that was worth removing.
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton)
                            transportBar.popupOverflowAt(videoMouse, mouse.x, mouse.y)
                    }
                    cursorShape: root.showPointer ? Qt.ArrowCursor : Qt.BlankCursor

                    // The wheel over the picture is volume, in the step the
                    // arrow keys use and with the same readout, so the two are
                    // one control reached two ways. Inside the MouseArea rather
                    // than beside it, so there is no question of which of the
                    // two sees the event first. The panel scrolls as it did:
                    // this is bounded by the picture.
                    WheelHandler {
                        id: volumeWheel

                        // A mouse notch is 120 units, but a trackpad and a
                        // high-resolution mouse send a stream of much smaller
                        // deltas instead. Stepping once per event would turn a
                        // single flick into forty steps and slam the volume to
                        // one end, so the deltas are accumulated and spent a
                        // notch at a time.
                        property real pending: 0

                        onWheel: (event) => {
                            volumeWheel.pending += event.angleDelta.y
                            // Truncated toward zero, so a half-notch is held
                            // rather than rounded into a step. It is also what
                            // makes a sideways scroll harmless: no vertical
                            // movement can never reach a whole notch.
                            var notches = Math.trunc(volumeWheel.pending / 120)
                            if (notches === 0)
                                return
                            volumeWheel.pending -= notches * 120
                            mpv.setVolume(mpv.volume + notches * prefs.volumeStep)
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

                // Drop feedback, so the cursor is not the only hint.
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
            SplitView.preferredWidth: root.defaultPanelWidth
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
        onAudioTrackPicked: (id) => {
            root.mpv.setAudioTrack(id)
            root.rememberAudioChoice(id)
        }
        onSubtitleTrackPicked: (track) => {
            // Named in mpv's numbering, so this owns the selection the way the
            // cycle does -- and for the same reason: the tab has not caught up
            // when the track-list change lands.
            root.subtitleSelectionIsOurs = true
            if (track === null) {
                root.wantedSubtitleId = -1
                root.mpv.setSubtitleTrack(-1)
            } else {
                root.wantedSubtitleId = track.id
                root.mpv.setSubtitleTrack(track.id)
                root.mpv.setSubtitleVisible(true)
                root.rememberMpvSubtitle(track)
            }
        }
        onQueueEntryPicked: (index) => {
            playlist.setCurrentPath(playlist.files[index])
            root.openFile(playlist.currentPath)
        }
        audioTracks: root.mpv.audioTracks
        subtitleTracks: root.mpv.subtitleTracks
        trackLabeller: root.trackLabel
    }
}
