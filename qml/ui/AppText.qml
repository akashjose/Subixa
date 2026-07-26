// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import Subixa

// Every piece of text in the application goes through here.
//
// It draws with the native `Text` item normally, and with `PaintedText` --
// QPainter into an ordinary texture -- when the graphics driver cannot be
// trusted to colour glyphs correctly. See PaintedText.h for what that means and
// how it was established; the short version is that Mesa's D3D12 driver under
// WSL renders Qt Quick's text materials in the wrong colour while every other
// primitive in the same frame is exact.
//
// Native is the default and stays the default. It shares one glyph atlas across
// the whole scene where this allocates a texture per item, and it is sharper.
// A healthy machine pays nothing for this existing, and the day Mesa is fixed
// the switch goes back to `gpu` and the painted path is dead code rather than a
// permanent tax.
//
// The API is the subset of `Text` the app uses. Resist growing it: anything more
// elaborate than a label belongs in a real Text with a comment explaining why it
// is safe there.
Item {
    id: root

    property string text: ""
    property color color: Theme.color.textPrimary
    property int textFormat: Text.PlainText
    property int wrapMode: Text.NoWrap
    property int elide: Text.ElideNone
    property int horizontalAlignment: Text.AlignLeft
    property int verticalAlignment: Text.AlignTop
    property real lineHeight: 1.0
    property int maximumLineCount: 0
    property real leftPadding: 0
    property real rightPadding: 0
    property alias font: metrics.font

    // Sizing comes from the same font metrics whichever item draws, so a layout
    // measures the same thing on both paths and switching cannot reflow the UI.
    TextMetrics {
        id: metrics
        text: root.text
    }

    readonly property bool painted: Theme.paintedText

    implicitWidth: item ? item.implicitWidth : 0
    implicitHeight: item ? item.implicitHeight : 0
    readonly property Item item: loader.item

    Loader {
        id: loader
        anchors.fill: parent
        sourceComponent: root.painted ? paintedComponent : nativeComponent
    }

    Component {
        id: nativeComponent

        Text {
            text: root.text
            color: root.color
            font: root.font
            textFormat: root.textFormat
            wrapMode: root.wrapMode
            elide: root.elide
            horizontalAlignment: root.horizontalAlignment
            verticalAlignment: root.verticalAlignment
            lineHeight: root.lineHeight
            lineHeightMode: Text.ProportionalHeight
            leftPadding: root.leftPadding
            rightPadding: root.rightPadding
            maximumLineCount: root.maximumLineCount > 0 ? root.maximumLineCount
                                                        : Number.MAX_VALUE
        }
    }

    Component {
        id: paintedComponent

        PaintedText {
            text: root.text
            color: root.color
            font: root.font
            textFormat: root.textFormat
            wrapMode: root.wrapMode
            elide: root.elide
            horizontalAlignment: root.horizontalAlignment
            verticalAlignment: root.verticalAlignment
            lineHeight: root.lineHeight
            leftPadding: root.leftPadding
            rightPadding: root.rightPadding
            maximumLineCount: root.maximumLineCount
        }
    }
}
