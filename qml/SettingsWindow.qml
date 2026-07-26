// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Subixa

// Preferences.
//
// A window rather than a modal dialog: someone checking subtitle timing wants
// this open beside the player, not blocking it. Everything applies live and
// there is no OK button -- a settings dialog with Apply/Cancel is a promise to
// roll back that this app has no mechanism to keep.
//
// It exists at all because the player had no settings surface whatsoever.
// Subtitle appearance -- the single thing users change most in any player, and
// the thing a subtitle-reading app most obviously needs -- was not configurable,
// and everything that was lived behind a 10px "More" button or a key nobody
// could discover.
Window {
    id: win

    required property var player
    required property var mpv
    required property var prefs
    required property var subStyle
    required property var shortcuts

    signal subtitleStyleChanged()
    signal textRenderingChanged()

    width: 880
    height: 640
    minimumWidth: 720
    minimumHeight: 520
    title: "Settings — Subixa"
    color: Theme.color.bgSurface
    visible: true

    property int page: 0
    readonly property var pages: [
        { label: "Playback",  icon: "play" },
        { label: "Subtitles", icon: "subtitles" },
        { label: "Browser",   icon: "list-video" },
        { label: "Hotkeys",   icon: "keyboard" },
        { label: "Interface", icon: "type" }
    ]

    function styleChanged() { win.subtitleStyleChanged() }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- sidebar --------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: Theme.color.bgRaised

            Divider {
                vertical: true
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.space.md
                spacing: Theme.space.xxs

                AppText {
                    Layout.fillWidth: true
                    Layout.margins: Theme.space.md
                    text: "Settings"
                    textFormat: Text.PlainText
                    color: Theme.color.textPrimary
                    font.family: Theme.type.sans
                    font.pixelSize: Theme.type.titleSize
                    font.weight: Theme.type.weightStrong
                }

                Repeater {
                    model: win.pages

                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        readonly property bool selected: win.page === index

                        Layout.fillWidth: true
                        implicitHeight: 36
                        radius: Theme.radius.md
                        color: selected ? Theme.color.accentSubtle
                             : itemHover.hovered ? Theme.color.bgHover : "transparent"

                        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }

                        // The same selection idiom as a subtitle row: a 3px
                        // accent rail. One selection language across the app.
                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.stroke.marker
                            height: parent.height - 12
                            radius: 1
                            color: Theme.color.accent
                            visible: parent.selected
                        }

                        HoverHandler { id: itemHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: win.page = index }

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.space.lg
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.space.md

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: modelData.icon
                                size: 17
                                color: parent.parent.selected ? Theme.color.accentText
                                                              : Theme.color.textSecondary
                            }

                            AppText {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                textFormat: Text.PlainText
                                color: parent.parent.selected ? Theme.color.accentText
                                                              : Theme.color.textSecondary
                                font.family: Theme.type.sans
                                font.pixelSize: Theme.type.bodySize
                                font.weight: parent.parent.selected
                                             ? Theme.type.weightStrong
                                             : Theme.type.weightNormal
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ---- content --------------------------------------------------
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: content.implicitHeight + 2 * Theme.space.xxxl
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            ColumnLayout {
                id: content
                x: Theme.space.xxxl
                y: Theme.space.xxxl
                // Form rows stretched to 900px are unreadable; cap the measure.
                width: Math.min(parent.width - 2 * Theme.space.xxxl, 620)
                spacing: Theme.space.lg

                // ================= PLAYBACK =================
                SectionCard {
                    visible: win.page === 0
                    title: "Playback"

                    FormRow {
                        label: "Play the next file automatically"
                        help: "When a file ends, move on to the next one in the folder."
                        AppSwitch {
                            checked: win.prefs.playNextAutomatically
                            onToggled: win.prefs.playNextAutomatically = checked
                        }
                    }

                    FormRow {
                        label: "Volume"
                        readout: Math.round(win.mpv.volume) + "%"
                        AppSlider {
                            Layout.fillWidth: true
                            from: 0; to: 150
                            value: win.mpv.volume
                            onMoved: win.mpv.setVolume(value)
                        }
                    }

                    FormRow {
                        label: "Volume step"
                        help: "How far the volume keys move it."
                        AppSpinBox {
                            value: win.prefs.volumeStep
                            from: 1; to: 25; step: 1; suffix: "%"
                            onMoved: (v) => win.prefs.volumeStep = v
                        }
                    }

                    FormRow {
                        label: "Seek step"
                        help: "The left and right arrow keys."
                        AppSpinBox {
                            value: win.prefs.seekStep
                            from: 1; to: 60; step: 1; suffix: " s"
                            onMoved: (v) => win.prefs.seekStep = v
                        }
                    }

                    FormRow {
                        label: "Large seek step"
                        help: "J and L."
                        AppSpinBox {
                            value: win.prefs.seekStepLarge
                            from: 5; to: 120; step: 5; suffix: " s"
                            onMoved: (v) => win.prefs.seekStepLarge = v
                        }
                    }

                    FormRow {
                        label: "Speed step"
                        Segmented {
                            options: [{ label: "0.05", value: 0.05 },
                                      { label: "0.1", value: 0.1 },
                                      { label: "0.25", value: 0.25 }]
                            value: win.prefs.speedStep
                            onActivated: (v) => win.prefs.speedStep = v
                        }
                    }

                    FormRow {
                        label: "Audio delay"
                        help: "Shifts the soundtrack against the picture."
                        readout: (win.mpv.audioDelay >= 0 ? "+" : "")
                                 + win.mpv.audioDelay.toFixed(3) + "s"
                        AppSpinBox {
                            value: win.mpv.audioDelay
                            from: -10; to: 10; step: 0.05; decimals: 2; suffix: " s"
                            onMoved: (v) => win.mpv.setAudioDelay(v)
                        }
                    }
                }

                SectionCard {
                    visible: win.page === 0
                    title: "Picture"

                    Repeater {
                        model: [
                            { key: "brightness", label: "Brightness" },
                            { key: "contrast",   label: "Contrast" },
                            { key: "saturation", label: "Saturation" },
                            { key: "gamma",      label: "Gamma" }
                        ]

                        delegate: FormRow {
                            required property var modelData
                            label: modelData.label
                            readout: String(adjust.value)
                            AppSlider {
                                id: adjust
                                Layout.fillWidth: true
                                from: -100; to: 100; stepSize: 1
                                value: win.mpv.videoAdjustment(modelData.key)
                                onMoved: win.mpv.setVideoAdjustment(modelData.key, value)
                            }
                        }
                    }
                }

                // ================= SUBTITLES =================
                SectionCard {
                    visible: win.page === 1
                    title: "Appearance on the video"

                    FormRow {
                        label: "Show subtitles"
                        AppSwitch {
                            checked: win.mpv.subtitleVisible
                            onToggled: win.mpv.setSubtitleVisible(checked)
                        }
                    }

                    FormRow {
                        label: "Font"
                        AppComboBox {
                            Layout.fillWidth: true
                            model: Qt.fontFamilies()
                            // Shown rather than derived from currentIndex: the
                            // stored value need not be in the list at all --
                            // "sans-serif" is a fontconfig alias, not a family --
                            // and a box displaying the first font on the system
                            // while mpv renders another is worse than useless.
                            displayText: win.subStyle.font
                            onActivated: {
                                win.subStyle.font = currentText
                                win.styleChanged()
                            }
                        }
                    }

                    FormRow {
                        label: "Size"
                        readout: String(win.subStyle.fontSize)
                        AppSlider {
                            Layout.fillWidth: true
                            from: 20; to: 120; stepSize: 1
                            value: win.subStyle.fontSize
                            onMoved: {
                                win.subStyle.fontSize = Math.round(value)
                                win.styleChanged()
                            }
                        }
                    }

                    FormRow {
                        label: "Weight and slant"
                        RowLayout {
                            spacing: Theme.space.sm
                            TextButton {
                                text: "Bold"
                                variant: "tonal"
                                checkable: true
                                checked: win.subStyle.bold
                                onClicked: {
                                    win.subStyle.bold = !win.subStyle.bold
                                    win.styleChanged()
                                }
                            }
                            TextButton {
                                text: "Italic"
                                variant: "tonal"
                                checkable: true
                                checked: win.subStyle.italic
                                onClicked: {
                                    win.subStyle.italic = !win.subStyle.italic
                                    win.styleChanged()
                                }
                            }
                        }
                    }

                    FormRow {
                        label: "Text colour"
                        ColorSwatch {
                            value: win.subStyle.color
                            onPicked: (c) => {
                                win.subStyle.color = c.toString()
                                win.styleChanged()
                            }
                        }
                    }

                    FormRow {
                        label: "Outline colour"
                        ColorSwatch {
                            value: win.subStyle.borderColor
                            onPicked: (c) => {
                                win.subStyle.borderColor = c.toString()
                                win.styleChanged()
                            }
                        }
                    }

                    FormRow {
                        label: "Outline width"
                        readout: win.subStyle.borderSize.toFixed(1)
                        AppSlider {
                            Layout.fillWidth: true
                            from: 0; to: 6; stepSize: 0.5
                            value: win.subStyle.borderSize
                            onMoved: {
                                win.subStyle.borderSize = value
                                win.styleChanged()
                            }
                        }
                    }

                    FormRow {
                        label: "Shadow"
                        readout: win.subStyle.shadowOffset.toFixed(1)
                        AppSlider {
                            Layout.fillWidth: true
                            from: 0; to: 6; stepSize: 0.5
                            value: win.subStyle.shadowOffset
                            onMoved: {
                                win.subStyle.shadowOffset = value
                                win.styleChanged()
                            }
                        }
                    }

                    FormRow {
                        label: "Vertical position"
                        help: "0 is the top of the frame, 100 the bottom."
                        readout: String(win.subStyle.pos)
                        AppSlider {
                            Layout.fillWidth: true
                            from: 0; to: 150; stepSize: 1
                            value: win.subStyle.pos
                            onMoved: {
                                win.subStyle.pos = Math.round(value)
                                win.styleChanged()
                            }
                        }
                    }

                    FormRow {
                        label: "Track styling"
                        help: "ASS tracks carry their own fonts and positions. "
                              + "Force ours to override them."
                        Segmented {
                            options: [{ label: "Track", value: "yes" },
                                      { label: "Force ours", value: "force" },
                                      { label: "Strip", value: "strip" }]
                            value: win.subStyle.assOverride
                            onActivated: (v) => {
                                win.subStyle.assOverride = v
                                win.styleChanged()
                            }
                        }
                    }
                }

                SectionCard {
                    visible: win.page === 1
                    title: "Timing"

                    FormRow {
                        label: "Subtitle delay"
                        help: "Positive means the subtitles arrive later."
                        readout: (win.mpv.subtitleDelay >= 0 ? "+" : "")
                                 + win.mpv.subtitleDelay.toFixed(3) + "s"
                        AppSpinBox {
                            value: win.mpv.subtitleDelay
                            from: -60; to: 60; step: 0.05; decimals: 2; suffix: " s"
                            onMoved: (v) => win.mpv.setSubtitleDelay(v)
                        }
                    }

                    FormRow {
                        label: "Loop lead-in"
                        help: "Extra time before a looped line, so its first "
                              + "syllable is not clipped."
                        AppSpinBox {
                            value: win.prefs.cueLoopLeadInMs
                            from: 0; to: 2000; step: 50; suffix: " ms"
                            onMoved: (v) => win.prefs.cueLoopLeadInMs = v
                        }
                    }

                    FormRow {
                        label: "Loop tail"
                        AppSpinBox {
                            value: win.prefs.cueLoopTailMs
                            from: 0; to: 2000; step: 50; suffix: " ms"
                            onMoved: (v) => win.prefs.cueLoopTailMs = v
                        }
                    }
                }

                // ================= BROWSER =================
                SectionCard {
                    visible: win.page === 2
                    title: "The subtitle browser"

                    FormRow {
                        label: "Row text size"
                        readout: String(Theme.rowFontSize)
                        AppSlider {
                            Layout.fillWidth: true
                            from: Theme.minimumRowFontSize
                            to: Theme.maximumRowFontSize
                            stepSize: 1
                            value: Theme.rowFontSize
                            onMoved: Theme.rowFontSize = Math.round(value)
                        }
                    }

                    FormRow {
                        label: "Show subtitle styling"
                        help: "Render the subtitler's own italics, bold and "
                              + "speaker colours in the list."
                        AppSwitch {
                            checked: Theme.showStyling
                            onToggled: Theme.showStyling = checked
                        }
                    }

                    FormRow {
                        label: "Show cue duration"
                        help: "Adds each line's length, flagged amber above 21 "
                              + "characters a second — faster than most people read."
                        AppSwitch {
                            checked: win.prefs.showCueDuration
                            onToggled: win.prefs.showCueDuration = checked
                        }
                    }

                    FormRow {
                        label: "Stop following when I scroll"
                        AppSwitch {
                            checked: win.prefs.followOffOnScroll
                            onToggled: win.prefs.followOffOnScroll = checked
                        }
                    }
                }

                // ================= HOTKEYS =================
                SectionCard {
                    visible: win.page === 3
                    title: "Keyboard"

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.md

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space.md

                            SearchField {
                                id: hotkeyFilter
                                Layout.fillWidth: true
                                // Searching by keystroke as well as by name is
                                // how you answer "what owns Ctrl+F".
                                placeholder: "Filter by action or key"
                            }

                            TextButton {
                                text: "Reset all"
                                variant: "danger"
                                onClicked: win.shortcuts.resetAll()
                            }
                        }

                        Repeater {
                            model: win.shortcuts.model()

                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool matches:
                                    hotkeyFilter.text === ""
                                    || modelData.label.toLowerCase().indexOf(
                                           hotkeyFilter.text.toLowerCase()) >= 0
                                    || modelData.sequence.toLowerCase().indexOf(
                                           hotkeyFilter.text.toLowerCase()) >= 0
                                    || modelData.category.toLowerCase().indexOf(
                                           hotkeyFilter.text.toLowerCase()) >= 0

                                Layout.fillWidth: true
                                visible: matches
                                implicitHeight: visible ? 40 : 0
                                color: capture.active ? Theme.color.bgSelected
                                     : rowHover.hovered ? Theme.color.bgHover
                                                        : "transparent"
                                radius: Theme.radius.md

                                HoverHandler { id: rowHover }

                                Divider {
                                    anchors.bottom: parent.bottom
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.space.md
                                    anchors.rightMargin: Theme.space.xs
                                    spacing: Theme.space.md

                                    ColumnLayout {
                                        spacing: 0

                                        AppText {
                                            text: modelData.label
                                            textFormat: Text.PlainText
                                            color: Theme.color.textPrimary
                                            font.family: Theme.type.sans
                                            font.pixelSize: Theme.type.bodySize
                                        }
                                        AppText {
                                            text: modelData.category
                                            textFormat: Text.PlainText
                                            color: Theme.color.textTertiary
                                            font.family: Theme.type.sans
                                            font.pixelSize: Theme.type.overlineSize
                                        }
                                    }

                                    // An explicit spacer, not Layout.fillWidth on
                                    // the label column above. Setting fillWidth
                                    // on a *nested layout* does not stretch it --
                                    // measured, not assumed -- so the key caps
                                    // and the buttons after them tracked the
                                    // length of each label instead of forming a
                                    // column. A spacer Item does stretch.
                                    Item { Layout.fillWidth: true }

                                    // Conflict warning, shown against the row
                                    // being edited rather than as a dialog.
                                    AppText {
                                        visible: capture.conflict !== ""
                                        text: "Used by " + capture.conflict
                                        textFormat: Text.PlainText
                                        color: Theme.color.warning
                                        font.family: Theme.type.sans
                                        font.pixelSize: Theme.type.captionSize
                                    }

                                    // A fixed, right-aligned column. Without it
                                    // the cap floats with the length of its own
                                    // label and the column of keys -- the thing
                                    // the eye scans down -- has no edge at all.
                                    Item {
                                        Layout.preferredWidth: 150
                                        Layout.fillHeight: true

                                        Chip {
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: capture.active
                                                  ? "Press a key…"
                                                  : (modelData.sequence === ""
                                                     ? "unbound" : modelData.sequence)
                                            tabular: true
                                            textColor: capture.active ? Theme.color.accentText
                                                     : modelData.sequence === ""
                                                       ? Theme.color.textTertiary
                                                     : modelData.isCustom
                                                       ? Theme.color.accentText
                                                       : Theme.color.textSecondary
                                        }
                                    }

                                    IconButton {
                                        size: Theme.size.iconButtonSm
                                        iconSize: 14
                                        iconName: capture.active ? "close" : "keyboard"
                                        tooltip: capture.active ? "Cancel" : "Rebind"
                                        onClicked: {
                                            if (capture.active) {
                                                capture.stop()
                                            } else {
                                                capture.begin(modelData.id)
                                            }
                                        }
                                    }

                                    IconButton {
                                        size: Theme.size.iconButtonSm
                                        iconSize: 14
                                        iconName: "repeat"
                                        enabled: modelData.isCustom
                                        tooltip: "Restore the default ("
                                                 + modelData.defaultSequence + ")"
                                        onClicked: win.shortcuts.resetSequence(modelData.id)
                                    }
                                }

                                // The capture field. Focus goes here while
                                // recording, and every key that is not a bare
                                // modifier becomes the new binding.
                                Item {
                                    id: capture
                                    property bool active: false
                                    property string conflict: ""
                                    focus: active

                                    function begin(id) {
                                        capture.active = true
                                        capture.conflict = ""
                                        capture.forceActiveFocus()
                                    }
                                    function stop() {
                                        capture.active = false
                                        capture.conflict = ""
                                    }

                                    Keys.onPressed: (event) => {
                                        if (!capture.active)
                                            return
                                        event.accepted = true
                                        if (event.key === Qt.Key_Escape) {
                                            capture.stop()
                                            return
                                        }
                                        if (event.key === Qt.Key_Backspace) {
                                            win.shortcuts.setSequence(modelData.id, "")
                                            capture.stop()
                                            return
                                        }
                                        // A modifier on its own is not a binding.
                                        if (event.key === Qt.Key_Control
                                            || event.key === Qt.Key_Shift
                                            || event.key === Qt.Key_Alt
                                            || event.key === Qt.Key_Meta)
                                            return

                                        var seq = ""
                                        if (event.modifiers & Qt.ControlModifier) seq += "Ctrl+"
                                        if (event.modifiers & Qt.AltModifier) seq += "Alt+"
                                        if (event.modifiers & Qt.ShiftModifier) seq += "Shift+"
                                        if (event.modifiers & Qt.MetaModifier) seq += "Meta+"
                                        seq += event.text !== "" && event.text.trim() !== ""
                                               ? event.text.toUpperCase()
                                               : String(event.key)

                                        var clash = win.shortcuts.conflict(seq, modelData.id)
                                        if (clash !== "") {
                                            capture.conflict = clash
                                            return
                                        }
                                        win.shortcuts.setSequence(modelData.id, seq)
                                        capture.stop()
                                    }
                                }
                            }
                        }
                    }
                }

                // ================= INTERFACE =================
                SectionCard {
                    visible: win.page === 4
                    title: "Appearance"

                    FormRow {
                        label: "Theme"
                        Segmented {
                            options: [{ label: "Dark", value: true },
                                      { label: "Light", value: false }]
                            value: Theme.dark
                            onActivated: (v) => Theme.dark = v
                        }
                    }

                    FormRow {
                        label: "Reduced motion"
                        help: "Removes the animations. The subtitle list still "
                              + "follows, it just jumps rather than scrolling."
                        AppSwitch {
                            checked: Theme.reducedMotion
                            onToggled: Theme.reducedMotion = checked
                        }
                    }

                    FormRow {
                        label: "Window size"
                        help: "The size a new window opens at."
                        readout: win.player.width + "×" + win.player.height
                        RowLayout {
                            spacing: Theme.space.sm
                            TextButton {
                                text: "Reset to " + win.player.defaultWindowWidth
                                      + "×" + win.player.defaultWindowHeight
                                variant: "tonal"
                                onClicked: win.player.resetWindowSize()
                            }
                        }
                    }

                    FormRow {
                        label: "Remember size and position"
                        // Worth saying plainly: this is why raising the default
                        // does nothing for anyone who has already moved the
                        // window, which is confusing enough to be worth a line.
                        help: "When off, every launch uses the default size "
                              + "instead of wherever you last left the window."
                        AppSwitch {
                            checked: win.prefs.rememberGeometry
                            onToggled: win.prefs.rememberGeometry = checked
                        }
                    }

                    FormRow {
                        label: "Text rendering"
                        help: win.mpv.glyphRenderingSuspect
                              ? "Auto has selected compatibility: this graphics "
                                + "driver (" + win.mpv.rendererName
                                + ") does not colour text correctly."
                              : "Auto uses the GPU, which is faster and sharper."
                        Segmented {
                            options: [{ label: "Auto", value: "auto" },
                                      { label: "GPU", value: "gpu" },
                                      { label: "Compatibility", value: "painted" }]
                            value: win.prefs.textRendering
                            onActivated: (v) => {
                                win.prefs.textRendering = v
                                win.textRenderingChanged()
                            }
                        }
                    }

                    FormRow {
                        label: "Visual effects"
                        help: win.mpv.softwareRendering
                              ? "Off automatically: this machine is rendering in "
                                + "software, where a shadow costs a whole pass."
                              : "Shadows and depth."
                        AppSwitch {
                            enabled: !win.mpv.softwareRendering
                            checked: Theme.effectsEnabled
                            onToggled: Theme.effectsEnabled = checked
                        }
                    }
                }

                SectionCard {
                    visible: win.page === 4
                    title: "About"

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.sm

                        AppText {
                            text: "Subixa " + Qt.application.version
                            textFormat: Text.PlainText
                            color: Theme.color.textPrimary
                            font.family: Theme.type.sans
                            font.pixelSize: Theme.type.bodySize
                            font.weight: Theme.type.weightStrong
                        }
                        AppText {
                            Layout.fillWidth: true
                            text: "Built on Qt " + Qt.application.version
                                  + " and libmpv. Graphics: "
                                  + (win.mpv.softwareRendering
                                     ? "software rasterizer" : "hardware GL") + "."
                            textFormat: Text.PlainText
                            color: Theme.color.textSecondary
                            font.family: Theme.type.sans
                            font.pixelSize: Theme.type.captionSize
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
