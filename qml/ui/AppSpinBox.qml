import QtQuick
import CustomMediaPlayer

// A numeric nudge: minus, value, plus. Not QtQuick.Controls' SpinBox, whose
// editable text field brings validation and focus questions this app has no use
// for -- every value here is picked, not typed.
Row {
    id: spin

    property real value: 0
    property real from: 0
    property real to: 100
    property real step: 1
    property int decimals: 0
    property string suffix: ""
    signal moved(real value)

    spacing: Theme.space.xxs

    function _clamp(v) { return Math.max(spin.from, Math.min(spin.to, v)) }
    function _nudge(d) {
        const next = spin._clamp(spin.value + d)
        if (next !== spin.value)
            spin.moved(next)
    }

    IconButton {
        size: Theme.size.iconButtonSm
        iconSize: 14
        iconName: "minus"
        enabled: spin.value > spin.from
        onClicked: spin._nudge(-spin.step)
        autoRepeat: true
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(52, implicitWidth)
        horizontalAlignment: Text.AlignHCenter
        text: spin.value.toFixed(spin.decimals) + spin.suffix
        textFormat: Text.PlainText
        color: Theme.color.textPrimary
        font.family: Theme.type.mono
        font.pixelSize: Theme.type.monoSize
    }

    IconButton {
        size: Theme.size.iconButtonSm
        iconSize: 14
        iconName: "plus"
        enabled: spin.value < spin.to
        onClicked: spin._nudge(spin.step)
        autoRepeat: true
    }
}
