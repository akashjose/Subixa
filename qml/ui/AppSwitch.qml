import QtQuick
import QtQuick.Templates as T
import Subixa

// An on/off switch with a state you can actually see. Basic's checked Button --
// which the Follow toggle used to be -- is a marginally darker grey, which on a
// dark theme is very nearly no state at all.
T.Switch {
    id: control

    implicitWidth: 36
    implicitHeight: 20
    hoverEnabled: true

    indicator: Rectangle {
        width: 36
        height: 20
        radius: height / 2
        anchors.verticalCenter: parent.verticalCenter
        color: !control.enabled ? Theme.color.bgPressed
             : control.checked ? (control.hovered ? Theme.color.accentHover
                                                  : Theme.color.accent)
                               : (control.hovered ? Theme.color.borderStrong
                                                  : Theme.color.bgPressed)

        Behavior on color {
            ColorAnimation { duration: Theme.motion.base; easing.type: Theme.motion.standard }
        }

        Rectangle {
            x: control.checked ? parent.width - width - 2 : 2
            y: 2
            width: 16
            height: 16
            radius: width / 2
            color: control.enabled ? Theme.color.knob : Theme.color.textDisabled

            Behavior on x {
                NumberAnimation { duration: Theme.motion.base; easing.type: Theme.motion.standard }
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.stroke.focus
            radius: height / 2
            color: "transparent"
            border.width: Theme.stroke.focus
            border.color: Theme.color.borderFocus
            visible: control.visualFocus
        }
    }

    contentItem: Item {}
}
