import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CustomMediaPlayer

// The subtitle browser itself: one tab per track, incremental search,
// click-to-seek, and auto-follow.
//
// Everything it works on is injected rather than reached for, so the same
// component runs docked in the main window or alone in a detached one. That is
// also what makes detaching cheap: the models live in C++ and are handed over by
// reference, so moving the panel between windows re-creates a few delegates and
// nothing else -- no reparse, no copy of 93 000 cues.
//
// Mutable view state (search text, selected tab, follow) lives in the `ui`
// object owned by the caller, because this item is destroyed and rebuilt each
// time it moves between windows.
Rectangle {
    id: panel
    // Named so the headless QML harness can find the panel and its parts in
    // either window without reaching through Loaders by index. Nothing in the
    // app uses these.
    objectName: "subtitlePanel"

    required property var manager     // SubtitleManager
    // Named linesModel, not lines: a property called `lines` would shadow the
    // caller's `lines` id inside this component's scope, and the binding
    // `lines: lines` would silently resolve to itself.
    required property var linesModel  // SubtitleFilterModel, shared with the caller
    required property var ui          // { searchText, tabIndex, following }
    property int currentRow: -1       // view row playing now, or -1
    property bool detached: false

    signal seekRequested(real seconds)
    signal trackActivated(int index)
    signal detachToggled()
    signal exportRequested()

    // Read by the window to decide whether a keystroke is a shortcut or typing.
    readonly property bool searchActive: searchField.activeFocus

    color: Theme.panelBackground

    function focusSearch() {
        searchField.forceActiveFocus()
        searchField.selectAll()
    }

    // Called when the row changes and when follow is switched back on.
    function showCurrent() {
        if (panel.ui.following)
            lineList.currentIndex = panel.currentRow
    }

    onCurrentRowChanged: panel.showCurrent()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            color: Theme.panelHeader

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                Label {
                    color: Theme.text
                    text: "Subtitles"
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Label {
                    color: panel.manager.busy ? Theme.busy : Theme.textDim
                    font.pixelSize: 11
                    // While searching, say how much of the track is showing;
                    // otherwise the parse result.
                    // Parsing a feature-length container takes seconds -- it has
                    // to be walked end to end whatever the cue count -- so say
                    // how far along it is rather than just "parsing subtitles…".
                    text: panel.manager.busy
                          ? panel.manager.status + " " + panel.manager.progress + "%"
                          : (panel.linesModel.pattern !== ""
                             ? panel.linesModel.count + " of " + panel.linesModel.sourceCount + " lines"
                             : panel.manager.status)
                }

                // Everything that is not read-a-track goes behind one button.
                // The header has to survive a 240 px panel, and detach, export
                // and the theme are all things you reach for once a session.
                Button {
                    id: moreButton
                    text: "More"
                    font.pixelSize: 10
                    padding: 4
                    flat: true
                    onClicked: moreMenu.popup(0, moreButton.height)

                    Menu {
                        id: moreMenu

                        MenuItem {
                            text: panel.detached ? "Dock the browser (Ctrl+D)"
                                                 : "Detach into its own window (Ctrl+D)"
                            onTriggered: panel.detachToggled()
                        }

                        MenuItem {
                            text: "Export this track as .srt…"
                            // Nothing to write before a track with lines is
                            // selected, and a save dialog that produces an empty
                            // file is worse than a disabled entry.
                            enabled: panel.linesModel.sourceCount > 0
                            onTriggered: panel.exportRequested()
                        }

                        MenuSeparator {}

                        MenuItem {
                            text: Theme.dark ? "Light theme" : "Dark theme"
                            onTriggered: Theme.dark = !Theme.dark
                        }

                        MenuItem {
                            text: "Show subtitle styling"
                            checkable: true
                            checked: Theme.showStyling
                            onTriggered: Theme.showStyling = checked
                        }

                        MenuItem {
                            text: "Larger text (Ctrl+=)"
                            enabled: Theme.rowFontSize < Theme.maximumRowFontSize
                            onTriggered: Theme.rowFontSize = Theme.rowFontSize + 1
                        }

                        MenuItem {
                            text: "Smaller text (Ctrl+-)"
                            enabled: Theme.rowFontSize > Theme.minimumRowFontSize
                            onTriggered: Theme.rowFontSize = Theme.rowFontSize - 1
                        }
                    }
                }
            }
        }

        TabBar {
            id: trackTabs
            objectName: "trackTabs"
            Layout.fillWidth: true
            visible: panel.manager.tracks.length > 0

            // Not a binding: TabBar assigns currentIndex itself on a click, which
            // would break one. The two are kept in step by hand instead.
            //
            // Restoring in Component.onCompleted was too early, and lost the tab
            // on every detach: the tabs come from a Repeater over the manager's
            // track list, so the bar is still empty when it completes, and a
            // Container adopts index 0 the moment it receives its first item.
            // That reset then wrote itself back into the caller's view state --
            // the one place it had to survive, since the panel itself is thrown
            // away on each move.
            //
            // So both directions are gated on the bar being *finished*: a bar
            // still filling up, or being torn down, has no opinion about which
            // track the reader chose. Only the tab count says which of those a
            // currentIndex change is.
            readonly property bool populated:
                count > 0 && count === panel.manager.tracks.length

            onPopulatedChanged: {
                if (populated && panel.ui.tabIndex >= 0
                    && panel.ui.tabIndex < count) {
                    currentIndex = panel.ui.tabIndex
                    showCurrentTab()
                }
            }

            // A remembered track on a film with 65 of them is usually off the
            // end of the bar, and a tab bar scrolled to the start while tab 65
            // is the live one reads as a bug. TabBar scrolls itself when the
            // reader clicks, but not when currentIndex is assigned.
            function showCurrentTab() {
                bringTabIntoView.restart()
            }

            // A short timer rather than Qt.callLater: the tabs are still being
            // laid out when the restore happens, and positioning a strip whose
            // contentWidth is not final yet does nothing at all.
            Timer {
                id: bringTabIntoView
                interval: 50
                onTriggered: {
                    if (trackTabs.contentItem && trackTabs.currentIndex >= 0) {
                        trackTabs.contentItem.positionViewAtIndex(
                            trackTabs.currentIndex, ListView.Contain)
                    }
                }
            }

            Connections {
                target: panel.ui
                function onTabIndexChanged() {
                    if (trackTabs.currentIndex !== panel.ui.tabIndex) {
                        trackTabs.currentIndex = panel.ui.tabIndex
                        // Also when the change came from the transport's Subs
                        // menu, which can land on a tab far off the end.
                        trackTabs.showCurrentTab()
                    }
                }
            }

            Repeater {
                model: panel.manager.tracks

                TabButton {
                    required property var modelData
                    text: modelData.label + " (" + modelData.lineCount + ")"
                    enabled: modelData.browsable
                    width: Math.max(implicitWidth, 72)
                    font.pixelSize: 11
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.codec + " · " + modelData.kind
                                  + (modelData.sidecar ? " · sidecar " + modelData.source : "")
                                  + (modelData.note !== "" ? "\n" + modelData.note : "")
                }
            }

            onCurrentIndexChanged: {
                if (!populated || currentIndex < 0)
                    return
                if (currentIndex === panel.ui.tabIndex)
                    return
                panel.ui.tabIndex = currentIndex
                panel.trackActivated(currentIndex)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8

            TextField {
                id: searchField
                objectName: "searchField"
                Layout.fillWidth: true
                placeholderText: "search…"
                enabled: panel.linesModel.sourceCount > 0
                font.pixelSize: 12

                // Restored rather than bound, for the same reason as the tab
                // index: typing would break a binding immediately.
                Component.onCompleted: text = panel.ui.searchText

                // Filtering walks every cue, so a 200k-line track would do that
                // work on each keystroke. Coalesce instead.
                onTextChanged: {
                    panel.ui.searchText = text
                    searchDebounce.restart()
                }
                Keys.onEscapePressed: text = ""

                Timer {
                    id: searchDebounce
                    interval: 150
                    onTriggered: panel.linesModel.pattern = searchField.text
                }
            }

            Button {
                id: followToggle
                text: "Follow"
                checkable: true
                font.pixelSize: 11
                padding: 6
                Component.onCompleted: checked = panel.ui.following
                ToolTip.visible: hovered
                ToolTip.text: "Scroll the list to the line playing now.\n"
                              + "Turns itself off if you drag the list."
                // Coming back on should jump to the current line rather than wait
                // for the next cue boundary.
                onCheckedChanged: {
                    panel.ui.following = checked
                    if (checked)
                        panel.showCurrent()
                }
            }
        }

        ListView {
            id: lineList
            objectName: "lineList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            clip: true
            spacing: 2
            model: panel.linesModel

            ScrollBar.vertical: ScrollBar {}

            // Keep the playing line in a band in the upper middle rather than at
            // a fixed point. A band gives hysteresis: the view only scrolls once
            // the line would leave it, so a run of short cues does not jog the
            // list on every single one, and there are always a few upcoming lines
            // visible below.
            //
            // Only while following -- with the range applied when follow is off,
            // scrolling by hand would be dragged back.
            highlightRangeMode: panel.ui.following ? ListView.ApplyRange
                                                   : ListView.NoHighlightRange
            preferredHighlightBegin: height * 0.3
            preferredHighlightEnd: height * 0.55
            // Animated, because an instant jump between distant cues (a seek, or
            // a gap in dialogue) loses the reader's place.
            highlightMoveDuration: 220
            highlightMoveVelocity: -1

            // A freshly built panel -- a detach, say -- should open on the line
            // playing now rather than at the top of the track.
            Component.onCompleted: panel.showCurrent()

            // Dragging the list is a statement of intent: stop yanking the
            // viewport back to the playing line.
            onDragStarted: followToggle.checked = false

            delegate: ItemDelegate {
                id: lineRow
                required property int index
                required property var model
                readonly property bool current: index === panel.currentRow

                width: lineList.width
                onClicked: panel.seekRequested(model.startMs / 1000)

                background: Rectangle {
                    color: lineRow.current
                           ? Theme.currentRow
                           : (lineRow.hovered ? Theme.hoverRow : "transparent")
                    border.color: lineRow.current ? Theme.accent : "transparent"
                    radius: 3
                }

                contentItem: ColumnLayout {
                    spacing: 1
                    Label {
                        color: lineRow.current ? Theme.timestampCurrent : Theme.timestamp
                        // Timestamps stay a step below the dialogue: they are
                        // there to be glanced at, not read.
                        font.pixelSize: Math.max(9, Theme.rowFontSize - 2)
                        font.family: "monospace"
                        text: lineRow.model.start
                    }
                    Label {
                        Layout.fillWidth: true
                        color: lineRow.current ? Theme.textStrong : Theme.text
                        font.pixelSize: Theme.rowFontSize
                        wrapMode: Text.WordWrap
                        // StyledText renders the subtitler's own italics, bold
                        // and speaker colours; PlainText is the escape hatch for
                        // a track that overuses them. Either way `text` is what
                        // search matches, so the two cannot disagree about which
                        // rows are showing.
                        textFormat: Theme.showStyling ? Text.StyledText
                                                      : Text.PlainText
                        text: Theme.showStyling ? lineRow.model.styled
                                                : lineRow.model.text
                    }
                }
            }

            Label {
                anchors.fill: parent
                visible: lineList.count === 0
                color: Theme.textDim
                wrapMode: Text.WordWrap
                verticalAlignment: Text.AlignTop
                text: panel.manager.tracks.length === 0
                      ? "No subtitle tracks in this file."
                      : panel.linesModel.pattern !== ""
                        ? "No lines match “" + panel.linesModel.pattern + "”."
                        : "Select a track above."
            }
        }
    }
}
