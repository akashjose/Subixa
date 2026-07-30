// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import QtQuick.Templates as T
import Subixa

// A menu row with a real shortcut column.
//
// The items this replaces carried their binding inside the label -- "Detach into
// its own window (Ctrl+D)" -- because Basic's MenuItem has nowhere else to put
// it. That reads as placeholder copy, produces ragged text, and cannot follow a
// remapped key.
T.MenuItem {
    id: item

    property string iconName: ""
    property string shortcutText: ""
    property bool danger: false

    // Measured from the fonts rather than from the labels themselves.
    //
    // AppText reports the size of whatever its Loader has produced, and that is
    // zero until the loaded item exists. Summing the two labels' implicitWidth
    // therefore measured a row of empty text, fell back to the 220 floor, and
    // left the longest entry -- "Open subtitle file…" against Ctrl+Shift+O --
    // with its shortcut printed on top of its label. Only the long ones showed
    // it, which is why it read as a rendering fault rather than a sizing one.
    //
    // TextMetrics answers synchronously and from the same font the label draws
    // with, so the width is right on the first frame and on both text paths.
    TextMetrics {
        id: labelMetrics
        text: item.text
        font.family: Theme.type.sans
        font.pixelSize: Theme.type.bodySize
    }

    TextMetrics {
        id: shortcutMetrics
        text: item.shortcutText
        font.family: Theme.type.mono
        font.pixelSize: Theme.type.captionSize
    }

    // The left margin, the fixed icon slot and its gap, the label, then -- only
    // when there is one -- a gap wide enough to read as a column break, the
    // shortcut, and the right margin. These mirror the anchors below; a change
    // there is a change here.
    implicitWidth: Math.max(220,
                            Theme.space.lg + 16 + Theme.space.md
                            + labelMetrics.width
                            + (item.shortcutText === ""
                               ? 0 : Theme.space.xl + shortcutMetrics.width)
                            + Theme.space.lg)
    implicitHeight: 30
    padding: 0
    hoverEnabled: true

    readonly property color _tint: !item.enabled ? Theme.color.textDisabled
                                : item.danger ? Theme.color.error
                                : item.highlighted ? Theme.color.textPrimary
                                                   : Theme.color.textSecondary

    background: Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Theme.space.xs
        anchors.rightMargin: Theme.space.xs
        radius: Theme.radius.md
        color: item.highlighted ? Theme.color.bgHover : "transparent"
    }

    contentItem: Item {
        Row {
            id: row
            anchors.left: parent.left
            anchors.leftMargin: Theme.space.lg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.md

            // A fixed slot, so labels line up whether or not an item has a mark.
            Item {
                width: 16
                height: 16
                anchors.verticalCenter: parent.verticalCenter

                Icon {
                    anchors.centerIn: parent
                    visible: item.checkable && item.checked
                    name: "check"
                    size: 14
                    color: Theme.color.accentText
                }
                Icon {
                    anchors.centerIn: parent
                    visible: !item.checkable && item.iconName !== ""
                    name: item.iconName
                    size: 15
                    color: item._tint
                }
            }

            AppText {
                anchors.verticalCenter: parent.verticalCenter
                text: item.text
                textFormat: Text.PlainText
                color: item._tint
                font.family: Theme.type.sans
                font.pixelSize: Theme.type.bodySize
            }
        }

        AppText {
            id: shortcutLabel
            anchors.right: parent.right
            anchors.rightMargin: Theme.space.lg
            anchors.verticalCenter: parent.verticalCenter
            text: item.shortcutText
            textFormat: Text.PlainText
            color: Theme.color.textTertiary
            font.family: Theme.type.mono
            font.pixelSize: Theme.type.captionSize
        }
    }
}
