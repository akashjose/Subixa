import QtQuick
import QtQuick.Templates as T
import CustomMediaPlayer

// A search box with a leading icon and a clear button.
//
// The stock TextField this replaces was a bare Basic-style box: no fill, no
// icon, no way to clear it but selecting the text, and a lowercase "search…"
// placeholder whose ellipsis meant nothing.
T.TextField {
    id: field

    property string placeholder: "Search"
    property bool showIcon: true
    signal cleared()

    implicitHeight: Theme.size.fieldHeight
    implicitWidth: 160
    leftPadding: (showIcon ? 26 : Theme.space.md)
    rightPadding: text !== "" ? 26 : Theme.space.md
    verticalAlignment: Text.AlignVCenter

    color: Theme.color.textPrimary
    selectionColor: Theme.color.accent
    selectedTextColor: Theme.color.onAccent
    font.family: Theme.type.sans
    font.pixelSize: Theme.type.bodySize
    // Deliberately not overridden. main() selects QtTextRendering for the whole
    // application because native rendering picks up fontconfig's RGB subpixel
    // antialiasing, which WSLg's RDP transport turns into coloured text -- see
    // the comment there. A local NativeRendering here would opt this one field
    // back into exactly that.

    background: Rectangle {
        radius: Theme.radius.md
        color: Theme.color.bgSunken
        border.width: field.activeFocus ? Theme.stroke.focus : Theme.stroke.hairline
        border.color: field.activeFocus ? Theme.color.borderFocus
                    : field.hovered ? Theme.color.borderStrong
                                    : Theme.color.border

        Behavior on border.color {
            ColorAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.standard }
        }
    }

    Icon {
        visible: field.showIcon
        anchors.left: parent.left
        anchors.leftMargin: Theme.space.md
        anchors.verticalCenter: parent.verticalCenter
        name: "search"
        size: 14
        color: field.activeFocus ? Theme.color.textSecondary : Theme.color.textTertiary
    }

    AppText {
        anchors.fill: parent
        anchors.leftMargin: field.leftPadding
        anchors.rightMargin: field.rightPadding
        verticalAlignment: Text.AlignVCenter
        visible: field.text === "" && !field.preeditText
        text: field.placeholder
        textFormat: Text.PlainText
        color: Theme.color.textTertiary
        font: field.font
        elide: Text.ElideRight
    }

    IconButton {
        visible: field.text !== ""
        anchors.right: parent.right
        anchors.rightMargin: Theme.space.xxs
        anchors.verticalCenter: parent.verticalCenter
        size: Theme.size.iconButtonSm - 4
        iconSize: 12
        iconName: "close"
        onClicked: {
            field.clear()
            field.cleared()
        }
    }
}
