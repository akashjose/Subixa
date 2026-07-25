import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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

    // Read by the window to decide whether a keystroke is a shortcut or typing.
    readonly property bool searchActive: searchField.activeFocus

    color: "#12121a"

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
            color: "#1b1b25"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                Label {
                    color: "#d0d0dc"
                    text: "Subtitles"
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Label {
                    color: panel.manager.busy ? "#c8a45c" : "#6a6a7a"
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

                Button {
                    text: panel.detached ? "Dock" : "Detach"
                    font.pixelSize: 10
                    padding: 4
                    flat: true
                    onClicked: panel.detachToggled()
                    ToolTip.visible: hovered
                    ToolTip.text: panel.detached
                                  ? "Put the browser back in the player window"
                                  : "Open the browser in its own window (Ctrl+D)"
                }
            }
        }

        TabBar {
            id: trackTabs
            Layout.fillWidth: true
            visible: panel.manager.tracks.length > 0

            // Not a binding: TabBar assigns currentIndex itself on a click, which
            // would break one. The two are kept in step by hand instead.
            Component.onCompleted: currentIndex = panel.ui.tabIndex

            Connections {
                target: panel.ui
                function onTabIndexChanged() {
                    if (trackTabs.currentIndex !== panel.ui.tabIndex)
                        trackTabs.currentIndex = panel.ui.tabIndex
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
                    color: lineRow.current ? "#23324a"
                                           : (lineRow.hovered ? "#1b1b25" : "transparent")
                    border.color: lineRow.current ? "#42618f" : "transparent"
                    radius: 3
                }

                contentItem: ColumnLayout {
                    spacing: 1
                    Label {
                        color: lineRow.current ? "#9fb6dc" : "#6f6f85"
                        font.pixelSize: 10
                        font.family: "monospace"
                        text: lineRow.model.start
                    }
                    Label {
                        Layout.fillWidth: true
                        color: lineRow.current ? "#ffffff" : "#d0d0dc"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        text: lineRow.model.text
                    }
                }
            }

            Label {
                anchors.fill: parent
                visible: lineList.count === 0
                color: "#6a6a7a"
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
