// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import QtQuick.Templates as T
import Subixa

// The workhorse control. Built on QtQuick.Templates rather than
// QtQuick.Controls: the template carries the behaviour (hover, press, checked,
// focus reason) and none of the Basic style's geometry or greys, so everything
// drawn here comes from the theme.
//
// `variant` covers the four ways a button appears in this app:
//   ghost   transparent until hovered -- the default, and what a toolbar wants
//   tonal   a filled but quiet button, for play/pause and other anchors
//   accent  the one primary action in a dialog
//   danger  destructive actions in settings
T.Button {
    id: control

    property string iconName: ""
    property string variant: "ghost"
    // A small accent dot at the lower right, for "a track is selected" and the
    // like. Cheaper to read than changing the icon.
    property bool badge: false
    property string tooltip: ""
    property string shortcutHint: ""
    property int size: Theme.size.iconButton
    property int iconSize: Math.round(size * 0.58)
    property int radius: Theme.radius.md

    implicitWidth: size
    implicitHeight: size
    hoverEnabled: true

    readonly property color _fill: {
        if (!control.enabled)
            return variant === "accent" ? Theme.color.accentDisabled : "transparent"
        if (variant === "accent")
            return control.pressed ? Theme.color.accentPressed
                 : control.hovered ? Theme.color.accentHover
                                   : Theme.color.accent
        if (control.checked)
            return control.hovered ? Theme.color.bgSelectedHover
                                   : Theme.color.accentSubtle
        if (control.pressed)
            return Theme.color.bgPressed
        if (control.hovered)
            return variant === "tonal" ? Theme.color.bgSelected : Theme.color.bgHover
        return variant === "tonal" ? Theme.color.bgPressed : "transparent"
    }

    readonly property color _tint: {
        if (!control.enabled)
            return Theme.color.textDisabled
        if (variant === "accent")
            return Theme.color.onAccent
        if (variant === "danger")
            return control.hovered ? Theme.color.error : Theme.color.textSecondary
        if (control.checked)
            return Theme.color.accentText
        if (control.hovered || control.pressed)
            return Theme.color.textPrimary
        return Theme.color.textSecondary
    }

    background: Rectangle {
        radius: control.radius
        color: control._fill

        Behavior on color {
            ColorAnimation {
                duration: Theme.motion.fast
                easing.type: Theme.motion.standard
            }
        }

        // Keyboard focus only. visualFocus is false when focus arrived by
        // click, which is what keeps a ring off every mouse press -- the classic
        // dated tell.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.stroke.focus
            radius: control.radius + Theme.stroke.focus
            color: "transparent"
            border.width: Theme.stroke.focus
            border.color: Theme.color.borderFocus
            visible: control.visualFocus
        }
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: control._tint
        }

        Rectangle {
            width: 5
            height: 5
            radius: width / 2
            color: Theme.color.accent
            visible: control.badge
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: Math.round(control.size * 0.12)
            anchors.bottomMargin: Math.round(control.size * 0.12)
        }
    }

    scale: control.pressed ? 0.94 : 1.0
    Behavior on scale {
        NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
    }

    ToolTipBubble {
        text: control.tooltip
        shortcut: control.shortcutHint
        visible: control.hovered && control.tooltip !== "" && !control.pressed
    }
}
