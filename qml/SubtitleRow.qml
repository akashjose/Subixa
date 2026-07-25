import QtQuick
import QtQuick.Templates as T
import CustomMediaPlayer

// One line of dialogue.
//
// This is the row the whole application exists to show, and the brief for it was
// exact: easy to follow, not distracting. Four decisions carry that.
//
//  1. The left rail carries the signal, the fill carries the context. A 3px
//     saturated bar at a fixed x is trackable in peripheral vision without
//     reading anything -- the eye locks onto a moving vertical mark far more
//     readily than onto a rectangle changing shade. The fill only says "this
//     block", quietly.
//  2. No border and no corner radius. The row this replaces was a tinted
//     rectangle with a 1px accent border and a 3px radius: four edges of
//     contrast around something already tinted and already rail-marked, which
//     is triple-encoding, and the rounded corners broke the continuous column
//     the eye scans down. That border was the distracting part.
//  3. Text brightens rather than recolours -- textSecondary to textPrimary, a
//     luminance step with no hue shift -- so the row gains weight without
//     changing character. Cue colours from the subtitler are untouched; they
//     still mean *speaker*.
//  4. The change crossfades over 140 ms. Below about 100 ms it strobes on rapid
//     dialogue; above 200 ms it lags the audio. Nothing moves, scales or glows:
//     a highlight that moves is what makes a follow-list unwatchable.
//
// The layout is a leading timestamp column rather than the timestamp stacked
// above the text. Stacking gave the eye no straight left edge to scan, which is
// the entire ergonomic point of a timestamped list, and cost about eight visible
// rows in a 700px panel.
T.ItemDelegate {
    id: row

    required property int index
    required property var model

    property bool current: false
    property bool wide: true
    property bool showEndTime: false
    property bool showCueDuration: false
    property int delayMs: 0

    signal seekRequested(int ms)
    signal copyRequested(bool withTimestamp)
    signal loopRequested()
    signal syncReferenceRequested()

    implicitHeight: Math.max(Theme.size.rowMin,
                             content.implicitHeight + 2 * Theme.space.sm)
    hoverEnabled: true

    // Seeking uses the delayed time so it lands where the line is spoken; the
    // column *displays* the stored time, which is what the file says.
    onClicked: row.seekRequested(row.model.startMs + row.delayMs)

    // The column is sized once from the widest timestamp it can hold, so it
    // stays put as the values tick past an hour -- and resizes correctly when
    // the reader changes the row font size.
    TextMetrics {
        id: timeMetrics
        font.family: Theme.type.mono
        font.pixelSize: Math.max(10, Theme.rowFontSize - 2)
        text: "00:00:00.000"
    }

    background: Rectangle {
        color: row.current
               ? (row.hovered ? Theme.color.bgSelectedHover : Theme.color.bgSelected)
               : (row.hovered ? Theme.color.bgHover : "transparent")

        Behavior on color {
            ColorAnimation {
                duration: Theme.motion.cueFade
                easing.type: Theme.motion.standard
            }
        }

        // The rail.
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Theme.stroke.marker
            color: row.current ? (row.hovered ? Theme.color.accentHover
                                              : Theme.color.cueMarker)
                 : row.hovered ? Theme.color.borderStrong : "transparent"

            Behavior on color {
                ColorAnimation {
                    duration: Theme.motion.cueFade
                    easing.type: Theme.motion.standard
                }
            }
        }
    }

    contentItem: Item {
        implicitHeight: content.implicitHeight

        // Wide: timestamp column, hairline, text.
        Row {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            visible: row.wide

            Item {
                width: timeMetrics.width + 2 * Theme.space.md
                height: Math.max(timeLabel.implicitHeight, 1)

                Column {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.md
                    anchors.top: parent.top
                    spacing: 1

                    AppText {
                        id: timeLabel
                        anchors.right: parent.right
                        text: row.model.start
                        textFormat: Text.PlainText
                        // Both timestamp tokens clear 4.5:1 on the current row
                        // as well as an ordinary one. The palette this replaces
                        // dropped to 2.64 on the highlight -- the highlight made
                        // the timestamp *harder* to read.
                        color: row.current ? Theme.color.timestampCurrent
                                           : Theme.color.timestamp
                        font.family: Theme.type.mono
                        font.pixelSize: Math.max(10, Theme.rowFontSize - 2)
                        font.weight: Theme.type.weightMedium

                        Behavior on color {
                            ColorAnimation { duration: Theme.motion.cueFade }
                        }
                    }

                    // Reading speed, for a QC pass. Over 21 characters a second
                    // is faster than most people read comfortably, which is the
                    // threshold every subtitling house checks against.
                    AppText {
                        anchors.right: parent.right
                        visible: row.showCueDuration
                        readonly property real seconds:
                            Math.max(0.001, (row.model.endMs - row.model.startMs) / 1000)
                        readonly property real cps: row.model.text.length / seconds
                        text: seconds.toFixed(1) + "s"
                        textFormat: Text.PlainText
                        color: cps > 21 ? Theme.color.warning : Theme.color.textTertiary
                        font.family: Theme.type.mono
                        font.pixelSize: Theme.type.overlineSize
                    }
                }
            }

            // The one divider that earns its keep: it establishes the scan edge.
            Rectangle {
                width: Theme.stroke.hairline
                height: parent.height
                color: Theme.color.borderSubtle
                opacity: 0.6
            }

            AppText {
                width: parent.width - x
                leftPadding: Theme.space.lg
                rightPadding: Theme.space.lg
                color: row.current ? Theme.color.textPrimary : Theme.color.textSecondary
                font.family: Theme.type.sans
                font.pixelSize: Theme.rowFontSize
                lineHeight: Theme.type.cueLine
                wrapMode: Text.WordWrap
                // StyledText renders the subtitler's own italics, bold and
                // speaker colours; PlainText is the escape hatch for a track
                // that overuses them. Either way it is `text` that search
                // matches, so the two cannot disagree about which rows show.
                textFormat: Theme.showStyling ? Text.StyledText : Text.PlainText
                text: Theme.showStyling ? row.model.styled : row.model.text

                Behavior on color {
                    ColorAnimation { duration: Theme.motion.cueFade }
                }
            }
        }

        // Narrow: stacked, because under 300px a time column plus wrapped text
        // leaves too little for either.
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.space.md
            anchors.rightMargin: Theme.space.md
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            visible: !row.wide

            AppText {
                text: row.model.start
                textFormat: Text.PlainText
                color: row.current ? Theme.color.timestampCurrent : Theme.color.timestamp
                font.family: Theme.type.mono
                font.pixelSize: Math.max(9, Theme.rowFontSize - 3)
            }

            AppText {
                width: parent.width
                color: row.current ? Theme.color.textPrimary : Theme.color.textSecondary
                font.family: Theme.type.sans
                font.pixelSize: Theme.rowFontSize
                wrapMode: Text.WordWrap
                textFormat: Theme.showStyling ? Text.StyledText : Text.PlainText
                text: Theme.showStyling ? row.model.styled : row.model.text
            }
        }
    }

    // Right-click. Every entry here is something a reader or a QC pass reaches
    // for on a specific line, which is why it is on the line rather than in a
    // menu somewhere above.
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: rowMenu.popup()
    }

    AppMenu {
        id: rowMenu
        preferAbove: false

        AppMenuItem {
            text: "Copy this line"
            onTriggered: row.copyRequested(false)
        }
        AppMenuItem {
            text: "Copy with its timestamp"
            onTriggered: row.copyRequested(true)
        }

        AppMenuSeparator {}

        AppMenuItem {
            text: "Seek here"
            onTriggered: row.seekRequested(row.model.startMs + row.delayMs)
        }
        AppMenuItem {
            text: "Loop this line"
            iconName: "repeat"
            onTriggered: row.loopRequested()
        }
        AppMenuItem {
            text: "Use as sync reference"
            iconName: "clock-arrows"
            onTriggered: row.syncReferenceRequested()
        }
    }
}
