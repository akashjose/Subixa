import QtQuick
// .Basic explicitly, for the same reason main() pins the style: every other
// file in the app names it, and a bare QtQuick.Controls import here would be
// the one place a platform style could get in.
import QtQuick.Controls.Basic as C
import CustomMediaPlayer

// A tooltip that follows the theme.
//
// An explicit instance rather than the attached ToolTip: the attached one is
// drawn by whatever Qt Quick Controls style is active, which is Basic, which
// would ignore every token here.
//
// The shortcut is a separate property rather than being written into the text,
// so it can be rendered as a key cap. The strings this replaces spelled it
// inline -- "Fullscreen (F)", "Larger text (Ctrl+=)" -- which reads as
// placeholder copy and cannot be kept in step with a remapped binding.
C.ToolTip {
    id: tip

    property string shortcut: ""

    delay: 500
    // The ToolTip stays on-screen by itself; only the offset is ours.
    y: -implicitHeight - Theme.space.md
    padding: 0

    enter: Transition {
        NumberAnimation {
            property: "opacity"; from: 0; to: 1
            duration: Theme.motion.fast; easing.type: Theme.motion.standard
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"; from: 1; to: 0
            duration: Theme.motion.fast; easing.type: Theme.motion.standard
        }
    }

    background: Rectangle {
        color: Theme.color.bgOverlay
        radius: Theme.radius.md
        border.width: Theme.stroke.hairline
        border.color: Theme.color.border
    }

    contentItem: Row {
        spacing: Theme.space.md
        leftPadding: Theme.space.md
        rightPadding: Theme.space.md
        topPadding: Theme.space.sm
        bottomPadding: Theme.space.sm

        AppText {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, 260)
            text: tip.text
            // PlainText everywhere, deliberately. A tooltip can carry a track
            // title straight out of a container's metadata, and Text.AutoText
            // promotes anything that looks like markup to full rich text -- which
            // supports <img src>, and would fetch it. A file is not allowed to
            // make the player issue a network request by being named cleverly.
            textFormat: Text.PlainText
            color: Theme.color.textPrimary
            font.family: Theme.type.sans
            font.pixelSize: Theme.type.captionSize
            wrapMode: Text.WordWrap
        }

        // A key cap, not "(F)" in the sentence.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: tip.shortcut !== ""
            width: shortcutLabel.implicitWidth + Theme.space.lg
            height: shortcutLabel.implicitHeight + Theme.space.sm
            radius: Theme.radius.sm
            color: Theme.color.bgSunken

            AppText {
                id: shortcutLabel
                anchors.centerIn: parent
                text: tip.shortcut
                textFormat: Text.PlainText
                color: Theme.color.textTertiary
                font.family: Theme.type.mono
                font.pixelSize: Theme.type.overlineSize
            }
        }
    }
}
