import QtQuick
import QtQuick.Templates as T
import CustomMediaPlayer

// A labelled button, optionally with a leading icon. Same variants and the same
// state table as IconButton, so the two sit beside each other without arguing.
T.Button {
    id: control

    property string iconName: ""
    property string variant: "ghost"
    property string tooltip: ""
    property string shortcutHint: ""
    // Monospaced digits, for anything whose label is a number that ticks -- the
    // speed readout, a delay. Stops the button reflowing on every change.
    property bool tabular: false

    implicitWidth: Math.max(row.implicitWidth + Theme.space.xl, 32)
    implicitHeight: Theme.size.iconButton
    hoverEnabled: true
    padding: 0

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
        radius: Theme.radius.md
        color: control._fill

        Behavior on color {
            ColorAnimation {
                duration: Theme.motion.fast
                easing.type: Theme.motion.standard
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.stroke.focus
            radius: Theme.radius.md + Theme.stroke.focus
            color: "transparent"
            border.width: Theme.stroke.focus
            border.color: Theme.color.borderFocus
            visible: control.visualFocus
        }
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: Theme.space.sm

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.iconName !== ""
                name: control.iconName
                size: 16
                color: control._tint
            }

            AppText {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                // See ToolTipBubble: PlainText everywhere. A button can carry a
                // track title out of a container, and AutoText would promote it
                // to rich text.
                textFormat: Text.PlainText
                color: control._tint
                font.family: control.tabular ? Theme.type.mono : Theme.type.sans
                font.pixelSize: Theme.type.labelSize
                font.weight: Theme.type.weightMedium

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.motion.fast
                        easing.type: Theme.motion.standard
                    }
                }
            }
        }
    }

    ToolTipBubble {
        text: control.tooltip
        shortcut: control.shortcutHint
        visible: control.hovered && control.tooltip !== "" && !control.pressed
    }
}
