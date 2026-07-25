import QtQuick
import Subixa

// A two-to-four way selector. Used wherever a setting has a small fixed set of
// answers and a dropdown would be heavier than the choice deserves.
Rectangle {
    id: seg

    property var options: []          // [{ label, value }] or plain strings
    property var value: undefined
    signal activated(var value)

    implicitWidth: row.implicitWidth + 2 * pad
    implicitHeight: 28
    radius: Theme.radius.md
    color: Theme.color.bgSunken

    readonly property int pad: 2

    function _label(o) { return o && o.label !== undefined ? o.label : o }
    function _value(o) { return o && o.value !== undefined ? o.value : o }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 0

        Repeater {
            model: seg.options

            delegate: Item {
                required property var modelData
                readonly property bool selected: seg._value(modelData) === seg.value

                width: Math.max(52, label.implicitWidth + Theme.space.xl)
                height: seg.height - 2 * seg.pad

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius.md - 1
                    color: parent.selected ? Theme.color.bgOverlay
                         : hover.hovered ? Theme.color.bgHover : "transparent"
                    border.width: parent.selected ? Theme.stroke.hairline : 0
                    border.color: Theme.color.border

                    Behavior on color {
                        ColorAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.standard }
                    }
                }

                AppText {
                    id: label
                    anchors.centerIn: parent
                    text: seg._label(parent.modelData)
                    textFormat: Text.PlainText
                    color: parent.selected ? Theme.color.textPrimary : Theme.color.textSecondary
                    font.family: Theme.type.sans
                    font.pixelSize: Theme.type.labelSize
                    font.weight: parent.selected ? Theme.type.weightStrong
                                                 : Theme.type.weightNormal
                }

                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: seg.activated(seg._value(parent.modelData)) }
            }
        }
    }
}
