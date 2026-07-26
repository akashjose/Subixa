// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import QtQuick.Templates as T
import Subixa

// A dropdown. Used where the option set is long enough that a Segmented row
// would not fit -- font families, hardware-decoding modes.
T.ComboBox {
    id: control

    implicitWidth: 200
    implicitHeight: Theme.size.fieldHeight
    leftPadding: Theme.space.lg
    rightPadding: 28
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.radius.md
        color: control.pressed ? Theme.color.bgPressed : Theme.color.bgSunken
        border.width: control.activeFocus ? Theme.stroke.focus : Theme.stroke.hairline
        border.color: control.activeFocus ? Theme.color.borderFocus
                    : control.hovered ? Theme.color.borderStrong : Theme.color.border

        Behavior on color {
            ColorAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.standard }
        }
    }

    contentItem: Text {
        text: control.displayText
        textFormat: Text.PlainText
        color: control.enabled ? Theme.color.textPrimary : Theme.color.textDisabled
        font.family: Theme.type.sans
        font.pixelSize: Theme.type.bodySize
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Icon {
        x: control.width - width - Theme.space.md
        y: (control.height - height) / 2
        name: "chevron-down"
        size: 14
        color: Theme.color.textTertiary
    }

    delegate: T.ItemDelegate {
        required property var model
        required property int index

        width: ListView.view.width
        implicitHeight: 30
        highlighted: control.highlightedIndex === index

        background: Rectangle {
            anchors.fill: parent
            anchors.leftMargin: Theme.space.xs
            anchors.rightMargin: Theme.space.xs
            radius: Theme.radius.md
            color: parent.highlighted ? Theme.color.bgHover : "transparent"
        }

        contentItem: Text {
            leftPadding: Theme.space.lg
            text: model[control.textRole] !== undefined ? model[control.textRole]
                                                        : String(model.modelData)
            textFormat: Text.PlainText
            color: control.currentIndex === index ? Theme.color.accentText
                                                  : Theme.color.textSecondary
            font.family: Theme.type.sans
            font.pixelSize: Theme.type.bodySize
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    popup: T.Popup {
        y: control.height + Theme.space.xs
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2 * Theme.space.xs, 320)
        padding: Theme.space.xs

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: AppScrollBar {}
        }

        background: Rectangle {
            color: Theme.color.bgOverlay
            radius: Theme.radius.lg
            border.width: Theme.stroke.hairline
            border.color: Theme.color.border
        }
    }
}
