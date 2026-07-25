import QtQuick
import Subixa

// A small pill carrying a count, a state or a key cap. Used for the queue
// position, cue counts beside a track name, the "cached" badge, and the A-B
// readout.
Rectangle {
    id: chip

    property string text: ""
    property string iconName: ""
    property color textColor: Theme.color.textSecondary
    property bool tabular: false
    property bool interactive: false
    signal clicked()

    implicitWidth: row.implicitWidth + Theme.space.lg
    implicitHeight: Math.max(20, row.implicitHeight + Theme.space.sm)
    radius: Theme.radius.pill
    color: interactive && hover.hovered ? Theme.color.bgPressed : Theme.color.bgSunken

    Behavior on color {
        ColorAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.standard }
    }

    HoverHandler { id: hover; enabled: chip.interactive; cursorShape: Qt.PointingHandCursor }
    TapHandler { enabled: chip.interactive; onTapped: chip.clicked() }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.space.xs

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.iconName !== ""
            name: chip.iconName
            size: 13
            color: chip.textColor
        }

        AppText {
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            textFormat: Text.PlainText
            color: chip.textColor
            font.family: chip.tabular ? Theme.type.mono : Theme.type.sans
            font.pixelSize: Theme.type.captionSize
            font.weight: Theme.type.weightMedium
        }
    }
}
