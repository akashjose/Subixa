// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import Subixa

// A colour button that opens a small palette. Used for subtitle text, outline
// and shadow colours.
Rectangle {
    id: swatch

    property color value: "#ffffff"
    // The colours subtitlers actually use, plus the neutrals. Not Theme tokens
    // and not meant to be: this is the palette offered for subtitles drawn over
    // the film, so it follows subtitling convention rather than the UI's scheme.
    property var presets: ["#ffffff", "#ffff00", "#00ffff", "#ff6666",
                           "#66ff66", "#ffaa00", "#cccccc", "#000000"]
    signal picked(color value)

    implicitWidth: 56
    implicitHeight: Theme.size.fieldHeight
    radius: Theme.radius.md
    color: Theme.color.bgSunken
    border.width: Theme.stroke.hairline
    border.color: hover.hovered ? Theme.color.borderStrong : Theme.color.border

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: palette.open() }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: Theme.radius.sm - 1
        color: swatch.value
        border.width: Theme.stroke.hairline
        border.color: Theme.color.tileEdge
    }

    AppMenu {
        id: palette
        implicitWidth: 8 * 26 + 2 * Theme.space.lg

        contentItem: Grid {
            columns: 8
            spacing: Theme.space.xs
            padding: Theme.space.md

            Repeater {
                model: swatch.presets
                delegate: Rectangle {
                    required property var modelData
                    width: 22
                    height: 22
                    radius: Theme.radius.sm
                    color: modelData
                    border.width: swatch.value == modelData ? 2 : Theme.stroke.hairline
                    border.color: swatch.value == modelData ? Theme.color.accent
                                                            : Theme.color.tileEdge
                    TapHandler {
                        onTapped: {
                            swatch.picked(modelData)
                            palette.close()
                        }
                    }
                }
            }
        }
    }
}
