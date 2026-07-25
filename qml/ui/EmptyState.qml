import QtQuick
import QtQuick.Layouts
import CustomMediaPlayer

// Icon, headline, explanation, and something to do about it.
//
// Every empty state in the app used to be a bare sentence in a dim grey --
// "No subtitle tracks in this file." -- which is a dead end rather than a state.
// Each one now offers the action that resolves it.
ColumnLayout {
    id: empty

    property string iconName: "info"
    property string title: ""
    property string body: ""
    property string actionText: ""
    property string actionShortcut: ""
    property bool compact: false
    signal actionTriggered()

    spacing: Theme.space.lg

    Icon {
        Layout.alignment: Qt.AlignHCenter
        name: empty.iconName
        size: empty.compact ? 32 : 56
        color: Theme.color.textDisabled
        animateColor: false
    }

    Text {
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 380
        visible: empty.title !== ""
        text: empty.title
        textFormat: Text.PlainText
        color: Theme.color.textPrimary
        font.family: Theme.type.sans
        font.pixelSize: empty.compact ? Theme.type.headingSize : Theme.type.displaySize
        font.weight: Theme.type.weightStrong
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 380
        visible: empty.body !== ""
        text: empty.body
        textFormat: Text.PlainText
        color: Theme.color.textSecondary
        font.family: Theme.type.sans
        font.pixelSize: Theme.type.bodySize
        lineHeight: Theme.type.bodyLine
        lineHeightMode: Text.ProportionalHeight
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Theme.space.xs
        spacing: Theme.space.md
        visible: empty.actionText !== ""

        TextButton {
            text: empty.actionText
            variant: "accent"
            onClicked: empty.actionTriggered()
        }

        Chip {
            visible: empty.actionShortcut !== ""
            text: empty.actionShortcut
            tabular: true
            textColor: Theme.color.textTertiary
        }
    }
}
