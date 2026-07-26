// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import QtQuick.Templates as T
import Subixa

// A slider for settings and volume. The thumb appears on hover rather than
// living permanently on the track: on a 72px volume control a always-visible
// handle is noise, and on a settings row the fill already says where the value
// is.
T.Slider {
    id: control

    property bool alwaysShowHandle: false

    implicitWidth: 160
    implicitHeight: 20

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: control.hovered || control.pressed ? 5 : Theme.stroke.track - 1
        radius: height / 2
        color: Theme.color.bgSunken

        Behavior on height {
            NumberAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.standard }
        }

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: control.enabled ? Theme.color.accent : Theme.color.accentDisabled
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 12
        height: 12
        radius: width / 2
        color: Theme.color.knob
        border.width: Theme.stroke.hairline
        border.color: Theme.color.knobEdge
        scale: control.pressed ? 1.25 : 1.0
        opacity: control.alwaysShowHandle || control.hovered || control.pressed
                 || control.visualFocus ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.standard }
        }
        Behavior on scale {
            NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
        }
    }
}
