// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import Subixa

// The seek bar, full-bleed across the top of the transport.
//
// Not a Slider. It carries five overlaid layers -- groove, buffered range,
// elapsed, marks, thumb -- plus a hover preview, and a Slider can express the
// first and the last of those. Full-bleed matters too: the stock control had to
// negotiate horizontal space with nine other controls in the same row, which is
// why it needed a 150px minimum, and 150px over a two-hour film is 48 seconds
// per pixel.
Item {
    id: bar

    property real position: 0
    property real duration: 0
    property real cacheEnd: 0
    property var chapters: []
    property real loopStart: -1
    property real loopEnd: -1
    // Optional: given a position in seconds, return the subtitle line there, or
    // "" for none. Shown in the hover preview -- the thing that makes this
    // player's seek bar worth more than anyone else's to someone reading along.
    property var cueAt: null
    // Over video the elapsed fill is white, which reads against any frame; in
    // the docked bar it is the accent.
    property bool overVideo: false

    signal seekRequested(real seconds)

    implicitHeight: Theme.size.seekStripHeight

    readonly property bool active: hover.hovered || drag.active
    readonly property real _ratio: duration > 0 ? Math.max(0, Math.min(1, position / duration)) : 0
    readonly property bool enabled_: duration > 0

    // Where the pointer is, as a fraction. -1 when it is not over the bar.
    property real hoverRatio: -1
    readonly property real hoverSeconds: hoverRatio * duration

    function _ratioAt(x) { return Math.max(0, Math.min(1, x / bar.width)) }

    function _fmt(t) {
        if (!isFinite(t) || t < 0)
            return "--:--"
        const s = Math.floor(t % 60)
        const m = Math.floor(t / 60) % 60
        const h = Math.floor(t / 3600)
        const two = (n) => (n < 10 ? "0" : "") + n
        return (h > 0 ? h + ":" + two(m) : m) + ":" + two(s)
    }

    // ---- track -------------------------------------------------------
    Rectangle {
        id: groove
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: bar.active ? Theme.stroke.trackHover : Theme.stroke.track
        radius: height / 2
        color: bar.overVideo ? Theme.color.onVideoTrack : Theme.color.bgSunken

        Behavior on height {
            NumberAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.standard }
        }

        // How far the demuxer has read ahead.
        Rectangle {
            height: parent.height
            radius: parent.radius
            color: Theme.color.onVideoBuffer
            visible: bar.duration > 0 && bar.cacheEnd > bar.position
            width: bar.duration > 0
                   ? Math.min(1, bar.cacheEnd / bar.duration) * parent.width : 0
        }

        // The A-B loop span.
        Rectangle {
            visible: bar.duration > 0 && bar.loopStart >= 0 && bar.loopEnd > bar.loopStart
            x: bar.duration > 0 ? (bar.loopStart / bar.duration) * parent.width : 0
            width: bar.duration > 0
                   ? ((bar.loopEnd - bar.loopStart) / bar.duration) * parent.width : 0
            height: parent.height
            color: Qt.alpha(Theme.color.warning, 0.35)
        }

        Rectangle {
            width: bar._ratio * parent.width
            height: parent.height
            radius: parent.radius
            color: bar.overVideo ? Theme.color.onVideo : Theme.color.accent
        }
    }

    // ---- chapter marks -----------------------------------------------
    Repeater {
        model: bar.duration > 0 ? bar.chapters : []

        delegate: Rectangle {
            required property var modelData
            // Chapter zero is the start of the file; a tick there is noise.
            visible: modelData.time > 0.5
            x: (modelData.time / bar.duration) * bar.width - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: bar.active ? Theme.stroke.trackHover : Theme.stroke.track
            color: bar.overVideo ? Theme.color.onVideoMark : Theme.color.borderStrong
        }
    }

    // ---- A/B markers --------------------------------------------------
    Repeater {
        model: [bar.loopStart, bar.loopEnd]

        delegate: Rectangle {
            required property var modelData
            visible: bar.duration > 0 && modelData >= 0
            x: bar.duration > 0 ? (modelData / bar.duration) * bar.width - width / 2 : 0
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: parent.height * 0.8
            radius: 1
            color: Theme.color.warning
        }
    }

    // ---- thumb --------------------------------------------------------
    Rectangle {
        x: bar._ratio * bar.width - width / 2
        anchors.verticalCenter: parent.verticalCenter
        width: 12
        height: 12
        radius: width / 2
        color: Theme.color.knob
        border.width: Theme.stroke.hairline
        border.color: Theme.color.knobEdge
        opacity: bar.active && bar.enabled_ ? 1 : 0
        scale: drag.active ? 1.25 : 1.0

        Behavior on opacity {
            NumberAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.standard }
        }
        Behavior on scale {
            NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
        }
    }

    // ---- input --------------------------------------------------------
    HoverHandler {
        id: hover
        enabled: bar.enabled_
        cursorShape: Qt.PointingHandCursor
        onPointChanged: bar.hoverRatio = bar._ratioAt(point.position.x)
        onHoveredChanged: if (!hovered) bar.hoverRatio = -1
    }

    TapHandler {
        enabled: bar.enabled_
        onTapped: (point) => bar.seekRequested(bar._ratioAt(point.position.x) * bar.duration)
    }

    DragHandler {
        id: drag
        enabled: bar.enabled_
        target: null
        xAxis.enabled: true
        yAxis.enabled: false
        onCentroidChanged: {
            if (!active)
                return
            bar.hoverRatio = bar._ratioAt(centroid.position.x)
            bar.seekRequested(bar.hoverRatio * bar.duration)
        }
    }

    WheelHandler {
        enabled: bar.enabled_
        onWheel: (event) => bar.seekRequested(
                     Math.max(0, Math.min(bar.duration,
                              bar.position + (event.angleDelta.y > 0 ? 5 : -5))))
    }

    // ---- hover preview -------------------------------------------------
    // Timestamp, and the line spoken there. One binary search per mouse move,
    // against cues that are already in memory.
    Item {
        id: preview
        visible: bar.hoverRatio >= 0 && bar.enabled_
        width: previewBox.width
        height: previewBox.height
        y: -height - Theme.space.md
        x: Math.max(Theme.space.md,
                    Math.min(bar.width - width - Theme.space.md,
                             bar.hoverRatio * bar.width - width / 2))

        readonly property string cueText:
            bar.cueAt && bar.hoverRatio >= 0 ? (bar.cueAt(bar.hoverSeconds) || "") : ""

        Rectangle {
            id: previewBox
            width: Math.max(previewTime.implicitWidth + 2 * Theme.space.lg,
                            Math.min(320, previewCue.implicitWidth + 2 * Theme.space.lg))
            height: previewColumn.implicitHeight + 2 * Theme.space.md
            radius: Theme.radius.md
            color: Theme.color.bgOverlay
            border.width: Theme.stroke.hairline
            border.color: Theme.color.border

            Column {
                id: previewColumn
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.space.lg
                spacing: Theme.space.xs

                AppText {
                    id: previewTime
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: bar._fmt(bar.hoverSeconds)
                    textFormat: Text.PlainText
                    color: Theme.color.textPrimary
                    font.family: Theme.type.mono
                    font.pixelSize: Theme.type.monoSize
                }

                AppText {
                    id: previewCue
                    width: parent.width
                    visible: preview.cueText !== ""
                    text: preview.cueText
                    // Cue text out of a subtitle file. Never AutoText.
                    textFormat: Text.PlainText
                    color: Theme.color.textSecondary
                    font.family: Theme.type.sans
                    font.pixelSize: Theme.type.captionSize
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
        }
    }
}
