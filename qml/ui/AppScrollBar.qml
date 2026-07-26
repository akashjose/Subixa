// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import QtQuick.Templates as T
import Subixa

// The scrollbar, which on a 93 000-row subtitle track is not decoration but the
// primary navigation instrument. It fades out when idle so it is not a
// permanent grey stripe down the panel, and it accepts marks -- search hits, the
// playing cue -- drawn over the track by whoever owns it.
T.ScrollBar {
    id: control

    // Items placed here are drawn over the track, positioned by the owner. The
    // subtitle list uses it to show where its matches are.
    default property alias marks: markLayer.data

    implicitWidth: 12
    padding: 2
    visible: control.size < 1.0
    policy: T.ScrollBar.AsNeeded

    readonly property bool active_: control.pressed || control.hovered
    opacity: active_ || control.active ? 1.0 : 0.0

    Behavior on opacity {
        NumberAnimation {
            duration: control.active_ ? Theme.motion.fast : Theme.motion.base
            easing.type: Theme.motion.standard
        }
    }

    background: Item {
        Item {
            id: markLayer
            anchors.fill: parent
            anchors.margins: control.padding
        }
    }

    contentItem: Rectangle {
        implicitWidth: control.pressed || control.hovered ? 8 : 4
        radius: width / 2
        color: control.pressed ? Theme.color.textSecondary
             : control.hovered ? Theme.color.textTertiary
                               : Theme.color.borderStrong

        Behavior on implicitWidth {
            NumberAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.standard }
        }
        Behavior on color {
            ColorAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.standard }
        }
    }
}
