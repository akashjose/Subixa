// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import QtQuick.Layouts
import Subixa

// A titled group of settings rows.
//
// The internal layout is assigned through `children:` rather than declared as a
// plain child. With `default property alias content` in scope, anything written
// as an ordinary child of this file's root is routed into that alias -- so the
// card's own column would be assigned into itself, and the resulting circular
// definition surfaces as a nonsense error a long way from the cause.
Rectangle {
    id: card

    property string title: ""
    default property alias content: inner.data

    Layout.fillWidth: true
    implicitHeight: column.implicitHeight + 2 * Theme.space.xl
    radius: Theme.radius.lg
    color: Theme.color.bgRaised
    border.width: Theme.stroke.hairline
    border.color: Theme.color.borderSubtle

    children: [
        ColumnLayout {
            id: column
            anchors.left: card.left
            anchors.right: card.right
            anchors.top: card.top
            anchors.margins: Theme.space.xl
            spacing: Theme.space.lg

            AppText {
                Layout.fillWidth: true
                visible: card.title !== ""
                text: card.title.toUpperCase()
                textFormat: Text.PlainText
                color: Theme.color.textTertiary
                font.family: Theme.type.sans
                font.pixelSize: Theme.type.overlineSize
                font.weight: Theme.type.weightStrong
                font.letterSpacing: 0.6
            }

            ColumnLayout {
                id: inner
                Layout.fillWidth: true
                spacing: Theme.space.lg
            }
        }
    ]
}
