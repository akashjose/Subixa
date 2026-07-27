// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import QtQuick.Layouts
import Subixa

// The transport: a full-bleed seek strip over a row of grouped controls.
//
// Two rows rather than one, and that is the change that matters most. The seek
// bar used to sit inline with nine other controls, negotiating horizontal space
// with all of them -- which is why it needed a 150px minimum, and 150px across a
// two-hour film is 48 seconds per pixel. Edge to edge on its own line it never
// has to negotiate, and the first and last second of the file become reachable.
//
// Everything is an icon. The controls this replaces were labelled in prose --
// "Play", "Vol", "Subs", "Full" -- and the play button changed width every time
// it was pressed, shoving the rest of the left cluster sideways.
Rectangle {
    id: bar

    required property var mpv
    required property var lines
    required property var playlist
    required property var shortcuts
    property bool overVideo: false
    property bool panelVisible: true
    property bool fullscreen: false
    property var audioTracks: []
    property var subtitleTracks: []
    property var trackLabeller: null

    signal action(string id)
    signal seekRequested(real seconds)
    signal audioTrackPicked(int id)
    signal subtitleTrackPicked(var track)
    signal queueEntryPicked(int index)

    implicitHeight: Theme.size.seekStripHeight + Theme.size.transportHeight
    color: overVideo ? "transparent" : Theme.color.bgRaised
    radius: overVideo ? Theme.radius.lg : 0

    // Named breakpoints rather than the six unnamed magic numbers this
    // replaces. Everything dropped stays reachable from the overflow menu *and*
    // a shortcut -- the overflow menu being the visible half of that promise.
    readonly property bool showVolumeSlider: width >= 720
    readonly property bool showSpeed: width >= 560
    readonly property bool showTrackButtons: width >= 420
    readonly property bool showQueue: width >= 420 && playlist.count > 1
    readonly property bool showDuration: width >= 380

    function key(id) { return bar.shortcuts.sequenceFor(id) }

    Rectangle {
        anchors.fill: parent
        visible: !bar.overVideo
        color: "transparent"
        border.width: 0

        Divider { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- seek strip: full-bleed, no horizontal margin ----------------
        SeekBar {
            id: seek
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.size.seekStripHeight
            position: bar.mpv.position
            duration: bar.mpv.duration
            cacheEnd: bar.mpv.cacheEnd
            chapters: bar.mpv.chapters
            loopStart: bar.mpv.loopStart
            loopEnd: bar.mpv.loopEnd
            overVideo: bar.overVideo
            // The hover preview shows the line spoken at that point. One binary
            // search per mouse move over cues already in memory -- and the
            // reason a subtitle reader would rather scrub here than anywhere.
            cueAt: (seconds) => bar.lines.textAt(Math.round(seconds * 1000))
            onSeekRequested: (seconds) => bar.seekRequested(seconds)
        }

        // ---- controls ----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.size.transportHeight
            Layout.leftMargin: Theme.space.lg
            Layout.rightMargin: Theme.space.lg
            spacing: Theme.space.xl

            // Playback cluster
            RowLayout {
                spacing: Theme.space.md

                IconButton {
                    iconName: "skip-back"
                    enabled: bar.playlist.hasPrevious
                    visible: bar.showQueue
                    tooltip: "Previous file"
                    shortcutHint: bar.key("previous-file")
                    onClicked: bar.action("previous-file")
                }

                IconButton {
                    // Fixed size and a fixed icon slot: the button must never
                    // reflow, and it labels the *state* rather than the action.
                    size: Theme.size.iconButtonLg
                    radius: Theme.radius.pill
                    variant: "tonal"
                    iconName: bar.mpv.paused ? "play" : "pause"
                    enabled: bar.mpv.duration > 0
                    tooltip: bar.mpv.paused ? "Play" : "Pause"
                    shortcutHint: bar.key("play-pause")
                    onClicked: bar.action("play-pause")
                }

                IconButton {
                    iconName: "skip-forward"
                    enabled: bar.playlist.hasNext
                    visible: bar.showQueue
                    tooltip: "Next file"
                    shortcutHint: bar.key("next-file")
                    onClicked: bar.action("next-file")
                }
            }

            // Clock. The position is the number people glance at most, so it is
            // primary and 13px rather than a muted 11; tabular figures stop the
            // digits jittering on every tick.
            RowLayout {
                spacing: 0

                AppText {
                    text: bar.fmtTime(bar.mpv.position)
                    textFormat: Text.PlainText
                    color: bar.overVideo ? Theme.color.onVideo : Theme.color.textPrimary
                    font.family: Theme.type.mono
                    font.pixelSize: Theme.type.monoSize
                }
                AppText {
                    visible: bar.showDuration
                    text: " / "
                    textFormat: Text.PlainText
                    color: bar.overVideo ? Theme.color.onVideoMuted : Theme.color.textTertiary
                    font.family: Theme.type.mono
                    font.pixelSize: Theme.type.monoSize
                }
                AppText {
                    visible: bar.showDuration
                    text: bar.fmtTime(bar.mpv.duration)
                    textFormat: Text.PlainText
                    color: bar.overVideo ? Theme.color.onVideoMuted : Theme.color.textSecondary
                    font.family: Theme.type.mono
                    font.pixelSize: Theme.type.monoSize
                }
            }

            // Says there is a queue at all, and where in it you are. Without it,
            // auto-advancing to another file looks like the player wandering off
            // on its own. Clickable, which is where the queue lives -- there is
            // deliberately no playlist panel competing with the browser.
            Chip {
                visible: bar.showQueue
                interactive: true
                iconName: "list-video"
                tabular: true
                text: (bar.playlist.currentIndex + 1) + "/" + bar.playlist.count
                onClicked: queueMenu.popupNear(this)
            }

            // A-B loop readout, present only while one is set.
            Chip {
                visible: bar.mpv.looping
                interactive: true
                iconName: "repeat"
                tabular: true
                textColor: Theme.color.warning
                text: bar.fmtTime(bar.mpv.loopStart) + "–" + bar.fmtTime(bar.mpv.loopEnd)
                onClicked: bar.action("loop-clear")
            }

            // Subtitle delay readout, present only while non-zero. A film being
            // watched at a 2-second offset should say so somewhere permanent.
            Chip {
                visible: Math.abs(bar.mpv.subtitleDelay) > 0.001
                interactive: true
                iconName: "clock-arrows"
                tabular: true
                textColor: Theme.color.accentText
                text: (bar.mpv.subtitleDelay >= 0 ? "+" : "")
                      + bar.mpv.subtitleDelay.toFixed(2) + "s"
                onClicked: bar.action("sub-delay-reset")
            }

            Item { Layout.fillWidth: true }

            // ---- right cluster -------------------------------------------
            RowLayout {
                spacing: Theme.space.md

                RowLayout {
                    spacing: Theme.space.xs

                    IconButton {
                        iconName: bar.mpv.muted ? "volume-mute"
                            : bar.mpv.volume < 1 ? "volume-mute"
                            : bar.mpv.volume < 40 ? "volume-low"
                            : bar.mpv.volume < 80 ? "volume-medium" : "volume-high"
                        tooltip: bar.mpv.muted ? "Unmute"
                                               : Math.round(bar.mpv.volume) + "%"
                        shortcutHint: bar.key("mute")
                        onClicked: bar.action("mute")
                    }

                    AppSlider {
                        visible: bar.showVolumeSlider
                        Layout.preferredWidth: 76
                        from: 0
                        to: 150
                        // Follows mpv rather than owning the value, so the
                        // keyboard and the slider cannot disagree.
                        value: bar.mpv.muted ? 0 : bar.mpv.volume
                        onMoved: {
                            if (bar.mpv.muted)
                                bar.mpv.setMuted(false)
                            bar.mpv.setVolume(value)
                        }
                    }
                }

                TextButton {
                    visible: bar.showSpeed || Math.abs(bar.mpv.speed - 1.0) > 0.01
                    // De-emphasised at exactly 1.0 and accent-coloured
                    // otherwise, so an off-normal speed announces itself.
                    text: bar.mpv.speed.toFixed(2).replace(/0$/, "") + "×"
                    tabular: true
                    tooltip: "Playback speed"
                    shortcutHint: bar.key("speed-up")
                    onClicked: speedMenu.popupNear(this)
                }

                IconButton {
                    visible: bar.showTrackButtons
                    iconName: "audio-track"
                    enabled: bar.mpv.duration > 0
                    badge: bar.mpv.audioTrack > 0
                    tooltip: "Audio track"
                    onClicked: audioMenu.popupNear(this)
                }

                IconButton {
                    visible: bar.showTrackButtons
                    iconName: bar.mpv.subtitleVisible && bar.mpv.subtitleTrack >= 0
                          ? "subtitles" : "subtitles-off"
                    enabled: bar.mpv.duration > 0
                    badge: bar.mpv.subtitleTrack >= 0
                    tooltip: "Subtitle track on the video"
                    shortcutHint: bar.key("subtitle-toggle")
                    onClicked: subMenu.popupNear(this)
                }

                IconButton {
                    // The panel had no button at all: Tab was the only way to
                    // reach it, which is undiscoverable.
                    iconName: "dock-right"
                    checkable: true
                    checked: bar.panelVisible
                    tooltip: bar.panelVisible ? "Hide the subtitle browser"
                                              : "Show the subtitle browser"
                    shortcutHint: bar.key("panel-toggle")
                    onClicked: bar.action("panel-toggle")
                }

                IconButton {
                    iconName: bar.fullscreen ? "fullscreen-exit" : "fullscreen-enter"
                    tooltip: bar.fullscreen ? "Leave fullscreen" : "Fullscreen"
                    shortcutHint: bar.key(bar.fullscreen ? "leave-fullscreen" : "fullscreen")
                    onClicked: bar.action("fullscreen")
                }

                IconButton {
                    // Always visible, rightmost: it absorbs whatever the
                    // breakpoints above drop, so nothing becomes unreachable.
                    iconName: "more-vertical"
                    tooltip: "More"
                    onClicked: overflowMenu.popupNear(this)
                }
            }
        }
    }

    function fmtTime(t) {
        if (!isFinite(t) || t < 0)
            return "--:--"
        var s = Math.floor(t % 60)
        var m = Math.floor(t / 60) % 60
        var h = Math.floor(t / 3600)
        var two = function (n) { return (n < 10 ? "0" : "") + n }
        return (h > 0 ? two(h) + ":" + two(m) : m) + ":" + two(s)
    }

    // The overflow menu, opened at a point in `item`'s coordinates instead of
    // against the button that normally summons it.
    //
    // The window calls this for a right-click on the picture. Deliberately the
    // same menu object rather than a second one built from the same actions: two
    // lists would agree on the day they were written and drift apart on the
    // first one that gains an entry.
    function popupOverflowAt(item, x, y) {
        overflowMenu.popupAtPoint(item, x, y)
    }

    // ---- menus -----------------------------------------------------------
    AppMenu {
        id: speedMenu

        Repeater {
            model: [0.5, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 2.0]
            AppMenuItem {
                required property var modelData
                text: modelData + "×"
                checkable: true
                // Float comparison with a tolerance: the step keys land on these
                // values but not necessarily on the same bit pattern.
                checked: Math.abs(bar.mpv.speed - modelData) < 0.01
                onTriggered: bar.mpv.setSpeed(modelData)
            }
        }
    }

    AppMenu {
        id: audioMenu

        Repeater {
            model: bar.audioTracks
            AppMenuItem {
                required property var modelData
                text: bar.trackLabeller ? bar.trackLabeller(modelData) : ""
                checkable: true
                checked: modelData.id === bar.mpv.audioTrack
                onTriggered: bar.audioTrackPicked(modelData.id)
            }
        }

        AppMenuSeparator {}

        AppMenuItem {
            text: "Off"
            checkable: true
            checked: bar.mpv.audioTrack < 0
            onTriggered: bar.audioTrackPicked(-1)
        }
    }

    AppMenu {
        id: subMenu

        Repeater {
            model: bar.subtitleTracks
            AppMenuItem {
                required property var modelData
                text: bar.trackLabeller ? bar.trackLabeller(modelData) : ""
                checkable: true
                checked: modelData.id === bar.mpv.subtitleTrack
                onTriggered: bar.subtitleTrackPicked(modelData)
            }
        }

        AppMenuSeparator {}

        AppMenuItem {
            text: "Off"
            checkable: true
            checked: bar.mpv.subtitleTrack < 0
            onTriggered: bar.subtitleTrackPicked(null)
        }

        AppMenuItem {
            text: "Show subtitles on the video"
            checkable: true
            checked: bar.mpv.subtitleVisible
            shortcutText: bar.key("subtitle-toggle")
            onTriggered: bar.action("subtitle-toggle")
        }

        AppMenuSeparator {}

        AppMenuItem {
            text: "Open a subtitle file…"
            iconName: "folder-open"
            shortcutText: bar.key("open-subtitle")
            onTriggered: bar.action("open-subtitle")
        }
    }

    // What the queue chip opens. A popover rather than a docked list: the
    // browser is the feature this player exists for, and a second permanent list
    // beside it would be the wrong thing to build.
    AppMenu {
        id: queueMenu
        implicitWidth: 380

        Repeater {
            model: bar.playlist.files
            AppMenuItem {
                required property var modelData
                required property int index
                text: {
                    var cut = String(modelData).lastIndexOf("/")
                    return cut >= 0 ? String(modelData).substring(cut + 1) : modelData
                }
                checkable: true
                checked: index === bar.playlist.currentIndex
                onTriggered: bar.queueEntryPicked(index)
            }
        }
    }

    AppMenu {
        id: overflowMenu

        AppMenuItem {
            text: "Open file…"
            iconName: "folder-open"
            shortcutText: bar.key("open-file")
            onTriggered: bar.action("open-file")
        }
        AppMenuItem {
            text: "Open subtitle file…"
            iconName: "subtitles"
            shortcutText: bar.key("open-subtitle")
            onTriggered: bar.action("open-subtitle")
        }

        AppMenuSeparator {}

        AppMenuItem {
            text: "Set loop start"
            iconName: "repeat"
            shortcutText: bar.key("loop-set-a")
            onTriggered: bar.action("loop-set-a")
        }
        AppMenuItem {
            text: "Set loop end"
            shortcutText: bar.key("loop-set-b")
            onTriggered: bar.action("loop-set-b")
        }
        AppMenuItem {
            text: "Loop this subtitle line"
            shortcutText: bar.key("loop-this-cue")
            onTriggered: bar.action("loop-this-cue")
        }
        AppMenuItem {
            text: "Clear the loop"
            enabled: bar.mpv.looping
            shortcutText: bar.key("loop-clear")
            onTriggered: bar.action("loop-clear")
        }

        AppMenuSeparator {}

        AppMenuItem {
            text: "Screenshot"
            iconName: "camera"
            shortcutText: bar.key("screenshot")
            onTriggered: bar.action("screenshot")
        }
        AppMenuItem {
            text: "Speed"
            iconName: "gauge"
            visible: !bar.showSpeed
            onTriggered: speedMenu.popupNear(bar)
        }

        AppMenuSeparator {}

        AppMenuItem {
            text: "Settings…"
            iconName: "settings"
            shortcutText: bar.key("settings")
            onTriggered: bar.action("settings")
        }
    }
}
