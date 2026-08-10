// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import QtQuick.Layouts
import Subixa
import "ui"

// Which track a file should open on, for one stream type. Subtitles and audio
// differ only in which flavour flags apply, so those are a property and
// everything else is shared.
//
// State is held here rather than bound to the store: PlaybackHistory is a
// settings file with no change notifications, so a binding would read once and
// then be wrong. Every edit writes through immediately -- this window has no OK
// button.
ColumnLayout {
    id: pref

    required property var history
    // "subtitle" or "audio".
    required property string type
    required property var manager   // SubtitleManager, for language names
    property bool hasForced: false
    property bool hasHearingImpaired: false
    property bool hasVisualImpaired: false

    property string mode: "learn"
    property var entries: []

    spacing: Theme.space.md

    // ISO 639-2/B, which is what ffmpeg writes into a container -- "ger" and
    // not "deu". Ordered by how often they are wanted, not alphabetically.
    readonly property var languageCodes: [
        "eng", "jpn", "kor", "chi", "spa", "por", "fre", "ger", "ita", "rus",
        "ara", "hin", "tam", "tel", "mal", "ben", "urd", "tur", "pol", "dut",
        "swe", "nor", "dan", "fin", "gre", "heb", "hun", "cze", "rum", "ukr",
        "vie", "tha", "ind", "may", "fil", "hrv", "srp", "bul", "cat", "per"
    ]

    function labelForCode(code) {
        if (code === undefined || code === "")
            return ""
        var name = pref.manager.languageName(code)
        return name === "" ? code : name + " (" + code + ")"
    }

    // Built once rather than forty QLocale lookups per delegate repaint.
    readonly property var languageLabels: {
        var out = []
        for (var i = 0; i < pref.languageCodes.length; ++i)
            out.push(pref.labelForCode(pref.languageCodes[i]))
        return out
    }

    Component.onCompleted: {
        pref.mode = pref.history.preferenceMode(pref.type)
        pref.entries = pref.history.preferenceList(pref.type)
    }

    function setMode(value) {
        pref.mode = value
        pref.history.setPreferenceMode(pref.type, value)
    }

    // Reassigning the whole array rather than mutating it is what makes the
    // Repeater notice: an array pushed into in place changes nothing QML sees.
    function commit(next) {
        pref.entries = next
        pref.history.setPreferenceList(pref.type, next)
    }

    function addEntry() {
        var next = pref.entries.slice()
        next.push({ language: "eng", forced: false,
                    hearingImpaired: false, visualImpaired: false })
        pref.commit(next)
    }

    function removeEntry(index) {
        var next = pref.entries.slice()
        next.splice(index, 1)
        pref.commit(next)
    }

    function moveEntry(index, delta) {
        var target = index + delta
        if (target < 0 || target >= pref.entries.length)
            return
        var next = pref.entries.slice()
        var moved = next[index]
        next[index] = next[target]
        next[target] = moved
        pref.commit(next)
    }

    function updateEntry(index, key, value) {
        var next = pref.entries.slice()
        // Copied for the same reason commit() reassigns: the old object may
        // still be the one a delegate is reading.
        var entry = {
            language: next[index].language,
            forced: next[index].forced === true,
            hearingImpaired: next[index].hearingImpaired === true,
            visualImpaired: next[index].visualImpaired === true
        }
        entry[key] = value
        next[index] = entry
        pref.commit(next)
    }

    FormRow {
        label: "Which track to open on"
        help: pref.mode === "file"
                  ? "Whatever the file itself flags as its default."
              : pref.mode === "learn"
                  ? "The track you last chose by hand, carried into files you "
                    + "have not opened before."
                  : "The list below, in order. Nothing matching falls back to "
                    + "the file's own default."
        Segmented {
            options: [
                { label: "Follow the file", value: "file" },
                { label: "Learn", value: "learn" },
                { label: "Explicit", value: "explicit" }
            ]
            value: pref.mode
            onActivated: (value) => pref.setMode(value)
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space.xxl
        visible: pref.mode === "explicit"
        spacing: Theme.space.sm

        Repeater {
            model: pref.entries

            RowLayout {
                id: entryRow
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: Theme.space.sm

                AppText {
                    text: (entryRow.index + 1) + "."
                    textFormat: Text.PlainText
                    color: Theme.color.textTertiary
                    font.family: Theme.type.mono
                    font.pixelSize: Theme.type.captionSize
                }

                AppComboBox {
                    Layout.preferredWidth: 190
                    model: pref.languageLabels
                    // Set rather than left to currentIndex, for the same reason
                    // the font box does it: a code the list does not carry must
                    // display as itself, not as whatever sits at index 0.
                    displayText: pref.labelForCode(entryRow.modelData.language)
                    currentIndex: pref.languageCodes.indexOf(entryRow.modelData.language)
                    onActivated: pref.updateEntry(entryRow.index, "language",
                                                  pref.languageCodes[currentIndex])
                }

                // Toggles rather than a second dropdown: a release can flag a
                // track both forced and SDH.
                Chip {
                    visible: pref.hasHearingImpaired
                    text: "SDH"
                    interactive: true
                    property bool on: entryRow.modelData.hearingImpaired === true
                    textColor: on ? Theme.color.accentText : Theme.color.textTertiary
                    border.width: on ? Theme.stroke.hairline : 0
                    border.color: Theme.color.accent
                    onClicked: pref.updateEntry(entryRow.index, "hearingImpaired", !on)
                }

                Chip {
                    visible: pref.hasForced
                    text: "Forced"
                    interactive: true
                    property bool on: entryRow.modelData.forced === true
                    textColor: on ? Theme.color.accentText : Theme.color.textTertiary
                    border.width: on ? Theme.stroke.hairline : 0
                    border.color: Theme.color.accent
                    onClicked: pref.updateEntry(entryRow.index, "forced", !on)
                }

                Chip {
                    visible: pref.hasVisualImpaired
                    text: "Audio description"
                    interactive: true
                    property bool on: entryRow.modelData.visualImpaired === true
                    textColor: on ? Theme.color.accentText : Theme.color.textTertiary
                    border.width: on ? Theme.stroke.hairline : 0
                    border.color: Theme.color.accent
                    onClicked: pref.updateEntry(entryRow.index, "visualImpaired", !on)
                }

                Item { Layout.fillWidth: true }

                IconButton {
                    iconName: "chevron-up"
                    tooltip: "Move up"
                    enabled: entryRow.index > 0
                    onClicked: pref.moveEntry(entryRow.index, -1)
                }
                IconButton {
                    iconName: "chevron-down"
                    tooltip: "Move down"
                    enabled: entryRow.index < pref.entries.length - 1
                    onClicked: pref.moveEntry(entryRow.index, 1)
                }
                IconButton {
                    iconName: "close"
                    tooltip: "Remove"
                    onClicked: pref.removeEntry(entryRow.index)
                }
            }
        }

        AppText {
            visible: pref.entries.length === 0
            text: "No preferences yet — the file's own default is used."
            textFormat: Text.PlainText
            color: Theme.color.textTertiary
            font.family: Theme.type.sans
            font.pixelSize: Theme.type.captionSize
        }

        TextButton {
            text: "Add a language"
            iconName: "plus"
            onClicked: pref.addEntry()
        }
    }
}
