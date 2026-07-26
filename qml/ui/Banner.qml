// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

import QtQuick
import Subixa

// The notice that appears over the picture. Over the video rather than in the
// transport bar, because a file that will not play is the one thing that must
// not be missed and the bar is hidden entirely in fullscreen.
Rectangle {
    id: banner

    // "error" | "success" | "info"
    property string severity: "info"
    property string message: ""
    property string actionText: ""
    signal actionTriggered()
    signal dismissed()

    readonly property color _fill: severity === "error" ? Theme.color.errorBg
                                 : severity === "success" ? Theme.color.successBg
                                                          : Theme.color.infoBg
    readonly property color _edge: severity === "error" ? Theme.color.error
                                 : severity === "success" ? Theme.color.success
                                                          : Theme.color.info
    readonly property color _text: severity === "error" ? Theme.color.errorText
                                 : severity === "success" ? Theme.color.successText
                                                          : Theme.color.infoText
    readonly property string _icon: severity === "error" ? "alert-triangle"
                                  : severity === "success" ? "check" : "info"

    implicitWidth: row.implicitWidth + 2 * Theme.space.lg
    implicitHeight: Math.max(40, row.implicitHeight + 2 * Theme.space.md)
    radius: Theme.radius.lg
    color: _fill
    border.width: Theme.stroke.hairline
    border.color: Qt.alpha(_edge, 0.4)

    // Hovering pauses the dismiss timer -- a nine-second banner that vanishes
    // while being read is worse than one that needs a click.
    property alias hovered: bannerHover.hovered
    HoverHandler { id: bannerHover }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.space.md

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: banner._icon
            size: 18
            color: banner._edge
        }

        AppText {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, 420)
            text: banner.message
            // Carries mpv error strings and filenames straight from disk.
            textFormat: Text.PlainText
            color: banner._text
            font.family: Theme.type.sans
            font.pixelSize: Theme.type.bodySize
            wrapMode: Text.WordWrap
        }

        TextButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: banner.actionText !== ""
            text: banner.actionText
            onClicked: banner.actionTriggered()
        }

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            size: Theme.size.iconButtonSm
            iconSize: 13
            iconName: "close"
            onClicked: banner.dismissed()
        }
    }
}
