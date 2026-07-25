import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CustomMediaPlayer

// The subtitle browser: one track at a time, incremental search, click-to-seek,
// auto-follow, and the timing controls a reader or a QC pass needs.
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
    required property var ui          // { searchText, tabIndex, following, syncBarVisible }
    required property var shortcuts
    property int currentRow: -1       // view row playing now, or -1
    property bool detached: false
    property real subtitleDelay: 0
    property bool showEndTime: false
    property bool showCueDuration: false
    property bool followOffOnScroll: true

    signal seekRequested(real seconds)
    signal trackActivated(int index)
    signal detachToggled()
    signal exportRequested()
    signal settingsRequested()
    signal openSubtitleRequested()
    signal delayNudged(real delta)
    signal delayReset()
    signal syncReferenceRequested(int row)
    signal loopRowRequested(int row)
    signal notify(string text, string severity)

    // Read by the window to decide whether a keystroke is a shortcut or typing.
    readonly property bool searchActive: searchField.activeFocus

    color: Theme.color.bgSurface

    // Below this the timestamp column and wrapped text leave too little for
    // either, so the row falls back to the stacked layout. 240 is the panel's
    // hard minimum width.
    readonly property bool wideRows: width >= 300
    // A strip of 65 tabs is unusable however it is styled. Past a handful the
    // selector becomes a searchable picker instead.
    readonly property bool useTabs: panel.manager.tracks.length <= 6 && width >= 320

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

    function copyRow(row, withTimestamp) {
        if (row < 0 || row >= panel.linesModel.count)
            return
        var text = panel.linesModel.textAtRow(row)
        var stamp = panel.linesModel.timestampAtRow(row)
        clipboard.text = withTimestamp ? (stamp + "\t" + text) : text
        clipboard.selectAll()
        clipboard.copy()
        panel.notify(withTimestamp ? "Line and timestamp copied" : "Line copied",
                     "success")
    }

    // A TextEdit is the only way QML offers to reach the clipboard without a
    // C++ helper. Zero-sized and never focused.
    TextEdit {
        id: clipboard
        visible: false
        width: 0
        height: 0
    }

    // ---- match navigation -------------------------------------------------
    // Which search hit the reader is on. A QC user hunting a term through a
    // 200k-cue track cannot do it by scrolling.
    property int matchCursor: -1
    function stepMatch(direction) {
        if (panel.linesModel.count === 0)
            return
        var next = panel.matchCursor + direction
        if (next < 0)
            next = panel.linesModel.count - 1
        if (next >= panel.linesModel.count)
            next = 0
        panel.matchCursor = next
        lineList.currentIndex = next
        lineList.positionViewAtIndex(next, ListView.Center)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- header -------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.size.panelHeaderHeight
            color: Theme.color.bgRaised

            Divider { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space.lg
                anchors.rightMargin: Theme.space.sm
                spacing: Theme.space.md

                Icon {
                    name: "subtitles"
                    size: 16
                    color: Theme.color.textSecondary
                }

                // The track being read, not the word "Subtitles". Someone
                // looking at the subtitle panel knows it is the subtitle panel;
                // which of 65 tracks they are reading is the useful thing.
                AppText {
                    Layout.fillWidth: true
                    text: {
                        var tracks = panel.manager.tracks
                        var t = tracks[panel.ui.tabIndex]
                        return t !== undefined ? t.label : "Subtitles"
                    }
                    textFormat: Text.PlainText
                    color: panel.manager.tracks.length > 0 ? Theme.color.textPrimary
                                                           : Theme.color.textTertiary
                    font.family: Theme.type.sans
                    font.pixelSize: Theme.type.headingSize
                    font.weight: Theme.type.weightStrong
                    elide: Text.ElideRight
                }

                // Parse progress, filter count, or the idle status -- the same
                // three states as before, but on tokens that clear 4.5:1. The
                // old readout sat at 3.22:1, which is the *progress* indicator
                // being the least readable thing in the panel.
                Chip {
                    visible: panel.manager.busy
                    iconName: "clock-arrows"
                    tabular: true
                    textColor: Theme.color.warning
                    text: panel.manager.progress + "%"
                }

                Chip {
                    visible: !panel.manager.busy && panel.linesModel.pattern !== ""
                    tabular: true
                    textColor: Theme.color.accentText
                    text: panel.linesModel.count + " / " + panel.linesModel.sourceCount
                }

                IconButton {
                    size: Theme.size.iconButtonSm
                    iconSize: 15
                    iconName: panel.detached ? "dock-right" : "external-link"
                    // Promoted out of the More menu: detaching is the panel's
                    // defining action and deserves a button.
                    tooltip: panel.detached ? "Dock the browser"
                                            : "Detach into its own window"
                    shortcutHint: panel.shortcuts.sequenceFor("panel-detach")
                    onClicked: panel.detachToggled()
                }

                IconButton {
                    id: moreButton
                    size: Theme.size.iconButtonSm
                    iconSize: 15
                    iconName: "more-vertical"
                    tooltip: "More"
                    onClicked: moreMenu.popupNear(moreButton)
                }
            }
        }

        // ---- track selector ------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 34
            visible: panel.manager.tracks.length > 0
            color: Theme.color.bgSurface

            Divider { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right }

            // A handful of tracks: a tab strip.
            TabBar {
                id: trackTabs
                objectName: "trackTabs"
                anchors.fill: parent
                visible: panel.useTabs
                background: Item {}

                // Not a binding: TabBar assigns currentIndex itself on a click,
                // which would break one. The two are kept in step by hand.
                //
                // Restoring in Component.onCompleted was too early, and lost the
                // tab on every detach: the tabs come from a Repeater over the
                // manager's track list, so the bar is still empty when it
                // completes, and a Container adopts index 0 the moment it
                // receives its first item. That reset then wrote itself back
                // into the caller's view state -- the one place it had to
                // survive, since the panel itself is thrown away on each move.
                //
                // So both directions are gated on the bar being *finished*: a
                // bar still filling up, or being torn down, has no opinion about
                // which track the reader chose.
                readonly property bool populated:
                    count > 0 && count === panel.manager.tracks.length

                onPopulatedChanged: {
                    if (populated && panel.ui.tabIndex >= 0
                        && panel.ui.tabIndex < count) {
                        currentIndex = panel.ui.tabIndex
                    }
                }

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
                        id: tabButton
                        required property var modelData
                        required property int index
                        enabled: modelData.browsable
                        implicitWidth: Math.max(tabRow.implicitWidth + Theme.space.xl, 72)
                        height: 34

                        background: Item {
                            // A 2px underline with rounded caps, not a filled
                            // box: tabs read as a strip rather than as buttons.
                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width - Theme.space.md
                                height: 2
                                radius: 1
                                color: Theme.color.accent
                                visible: tabButton.checked
                            }
                            Rectangle {
                                anchors.fill: parent
                                color: Theme.color.bgHover
                                visible: tabButton.hovered && !tabButton.checked
                            }
                        }

                        contentItem: Row {
                            id: tabRow
                            anchors.centerIn: parent
                            spacing: Theme.space.sm

                            AppText {
                                anchors.verticalCenter: parent.verticalCenter
                                text: tabButton.modelData.label
                                textFormat: Text.PlainText
                                color: !tabButton.enabled ? Theme.color.textDisabled
                                     : tabButton.checked ? Theme.color.textPrimary
                                     : tabButton.hovered ? Theme.color.textSecondary
                                                         : Theme.color.textTertiary
                                font.family: Theme.type.sans
                                font.pixelSize: Theme.type.labelSize
                                font.weight: tabButton.checked ? Theme.type.weightStrong
                                                               : Theme.type.weightNormal
                            }

                            // The cue count as a de-emphasised badge rather than
                            // welded into the label, so the two stop competing
                            // at the same weight.
                            AppText {
                                anchors.verticalCenter: parent.verticalCenter
                                text: tabButton.modelData.lineCount
                                textFormat: Text.PlainText
                                color: Theme.color.textTertiary
                                font.family: Theme.type.mono
                                font.pixelSize: Theme.type.overlineSize
                            }
                        }

                        ToolTipBubble {
                            text: tabButton.modelData.codec + " · " + tabButton.modelData.kind
                                  + (tabButton.modelData.sidecar
                                     ? " · sidecar " + tabButton.modelData.source : "")
                                  + (tabButton.modelData.note !== ""
                                     ? "\n" + tabButton.modelData.note : "")
                            visible: tabButton.hovered
                        }
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

            // Many tracks: a picker. The 65-track film is the case this exists
            // for -- no amount of scroll-positioning makes 65 tabs good.
            Item {
                anchors.fill: parent
                visible: !panel.useTabs

                TextButton {
                    id: trackPicker
                    anchors.fill: parent
                    anchors.margins: Theme.space.xs
                    text: {
                        var t = panel.manager.tracks[panel.ui.tabIndex]
                        return t !== undefined
                             ? t.label + "  ·  " + t.lineCount + " lines"
                             : "Select a track"
                    }
                    onClicked: trackMenu.popupNear(trackPicker)
                }

                Icon {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.lg
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron-down"
                    size: 14
                    color: Theme.color.textTertiary
                }
            }
        }

        // ---- search + follow ------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            color: Theme.color.bgSurface

            Divider { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right }

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.space.md
                spacing: Theme.space.sm

                SearchField {
                    id: searchField
                    objectName: "searchField"
                    Layout.fillWidth: true
                    placeholder: "Search subtitles"
                    showIcon: panel.width >= 280
                    enabled: panel.linesModel.sourceCount > 0

                    // Restored rather than bound, for the same reason as the tab
                    // index: typing would break a binding immediately.
                    Component.onCompleted: text = panel.ui.searchText

                    // Filtering walks every cue, so a 200k-line track would do
                    // that work on each keystroke. Coalesce instead.
                    onTextChanged: {
                        panel.ui.searchText = text
                        searchDebounce.restart()
                    }
                    Keys.onEscapePressed: text = ""
                    onCleared: panel.matchCursor = -1

                    Timer {
                        id: searchDebounce
                        interval: 150
                        onTriggered: {
                            panel.linesModel.pattern = searchField.text
                            panel.matchCursor = -1
                        }
                    }
                }

                // Jump between matches.
                RowLayout {
                    visible: panel.linesModel.pattern !== "" && panel.width >= 320
                    spacing: 0

                    IconButton {
                        size: Theme.size.iconButtonSm
                        iconSize: 13
                        iconName: "chevron-up"
                        onClicked: panel.stepMatch(-1)
                    }
                    IconButton {
                        size: Theme.size.iconButtonSm
                        iconSize: 13
                        iconName: "chevron-down"
                        onClicked: panel.stepMatch(1)
                    }
                }

                IconButton {
                    size: Theme.size.iconButtonSm
                    iconSize: 15
                    iconName: "crosshair-lock"
                    checkable: true
                    checked: panel.ui.following
                    // Checked is an accent-tinted fill with an accent icon --
                    // unmistakable, unlike Basic's checked button, which on a
                    // dark theme is a marginally darker grey.
                    tooltip: "Scroll the list to the line playing now.\n"
                             + "Turns itself off if you drag the list."
                    shortcutHint: panel.shortcuts.sequenceFor("follow-toggle")
                    onClicked: {
                        panel.ui.following = !panel.ui.following
                        if (panel.ui.following)
                            panel.showCurrent()
                    }
                }
            }
        }

        // ---- subtitle timing --------------------------------------------
        // Appears once the delay is touched. The QC and language-learning
        // audience this player is for cannot do their work without it.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 36
            visible: panel.ui.syncBarVisible || Math.abs(panel.subtitleDelay) > 0.001
            color: Theme.color.bgRaised

            Divider { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space.lg
                anchors.rightMargin: Theme.space.sm
                spacing: Theme.space.md

                Icon {
                    name: "clock-arrows"
                    size: 15
                    color: Theme.color.accentText
                }

                AppText {
                    visible: panel.width >= 340
                    text: "Delay"
                    textFormat: Text.PlainText
                    color: Theme.color.textSecondary
                    font.family: Theme.type.sans
                    font.pixelSize: Theme.type.labelSize
                }

                Item { Layout.fillWidth: true }

                IconButton {
                    size: Theme.size.iconButtonSm
                    iconSize: 13
                    iconName: "minus"
                    autoRepeat: true
                    tooltip: "Subtitles earlier (hold Shift for 0.5 s)"
                    onClicked: panel.delayNudged(-0.05)
                }

                AppText {
                    text: (panel.subtitleDelay >= 0 ? "+" : "")
                          + panel.subtitleDelay.toFixed(3) + " s"
                    textFormat: Text.PlainText
                    color: Math.abs(panel.subtitleDelay) > 0.001
                           ? Theme.color.textPrimary : Theme.color.textTertiary
                    font.family: Theme.type.mono
                    font.pixelSize: Theme.type.monoSize
                }

                IconButton {
                    size: Theme.size.iconButtonSm
                    iconSize: 13
                    iconName: "plus"
                    autoRepeat: true
                    tooltip: "Subtitles later"
                    onClicked: panel.delayNudged(0.05)
                }

                IconButton {
                    size: Theme.size.iconButtonSm
                    iconSize: 13
                    iconName: "close"
                    tooltip: "Reset the delay"
                    shortcutHint: panel.shortcuts.sequenceFor("sub-delay-reset")
                    onClicked: {
                        panel.delayReset()
                        panel.ui.syncBarVisible = false
                    }
                }
            }
        }

        // ---- the list ------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: lineList
                objectName: "lineList"
                anchors.fill: parent
                clip: true
                // No gap between rows and no list margin: rows are contiguous so
                // the eye tracks one continuous column down the timestamps.
                spacing: 0
                model: panel.linesModel
                visible: count > 0
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true

                ScrollBar.vertical: AppScrollBar {
                    id: listScroll

                    // The scrollbar as a map: where the current cue is, and
                    // where the matches are. On a 93 000-row list this is the
                    // difference between navigable and not.
                    Rectangle {
                        parent: listScroll.background
                        visible: panel.currentRow >= 0 && lineList.count > 0
                        width: parent ? parent.width : 0
                        height: 3
                        radius: 1
                        color: Theme.color.accent
                        y: parent && lineList.count > 0
                           ? (panel.currentRow / lineList.count) * parent.height : 0
                    }
                }

                // Keep the playing line in a band in the upper middle rather than
                // at a fixed point. A band gives hysteresis: the view only
                // scrolls once the line would leave it, so a run of short cues
                // does not jog the list on every single one, and there are
                // always a few upcoming lines visible below.
                //
                // Only while following -- with the range applied when follow is
                // off, scrolling by hand would be dragged back.
                highlightRangeMode: panel.ui.following ? ListView.ApplyRange
                                                       : ListView.NoHighlightRange
                preferredHighlightBegin: height * 0.3
                preferredHighlightEnd: height * 0.55
                // Animated, because an instant jump between distant cues (a
                // seek, or a gap in dialogue) loses the reader's place.
                highlightMoveDuration: Theme.motion.follow
                highlightMoveVelocity: -1

                // A freshly built panel -- a detach, say -- should open on the
                // line playing now rather than at the top of the track.
                Component.onCompleted: panel.showCurrent()

                // Dragging the list is a statement of intent: stop yanking the
                // viewport back to the playing line.
                onDragStarted: {
                    if (panel.followOffOnScroll)
                        panel.ui.following = false
                }

                delegate: SubtitleRow {
                    width: lineList.width
                    wide: panel.wideRows
                    current: index === panel.currentRow
                    showEndTime: panel.showEndTime
                    showCueDuration: panel.showCueDuration
                    delayMs: Math.round(panel.subtitleDelay * 1000)
                    onSeekRequested: (ms) => panel.seekRequested(ms / 1000)
                    onCopyRequested: (withStamp) => panel.copyRow(index, withStamp)
                    onLoopRequested: panel.loopRowRequested(index)
                    onSyncReferenceRequested: panel.syncReferenceRequested(index)
                }
            }

            // ---- empty states ---------------------------------------------
            // Each one offers the action that resolves it. The bare sentences
            // these replace were dead ends.
            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 320)
                visible: lineList.count === 0 && !panel.manager.busy
                         && panel.manager.tracks.length === 0
                compact: true
                iconName: "subtitles-off"
                title: "No subtitles in this file"
                body: "Nothing embedded, and no sidecar next to it."
                actionText: "Load a subtitle file…"
                onActionTriggered: panel.openSubtitleRequested()
            }

            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 320)
                visible: lineList.count === 0 && !panel.manager.busy
                         && panel.manager.tracks.length > 0
                         && panel.linesModel.pattern !== ""
                compact: true
                iconName: "search"
                title: "Nothing matches"
                body: "No lines contain “" + panel.linesModel.pattern + "”."
                actionText: "Clear search"
                onActionTriggered: searchField.clear()
            }

            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 320)
                visible: lineList.count === 0 && !panel.manager.busy
                         && panel.manager.tracks.length > 0
                         && panel.linesModel.pattern === ""
                compact: true
                iconName: "subtitles"
                title: "Select a track"
                body: "This file has tracks, but none is being shown."
            }

            // ---- parsing ----------------------------------------------------
            // Skeleton rows rather than a spinner: they say "this is a list and
            // it is coming". Held back 250 ms so a cache hit -- about 110 ms on
            // the 65-track film -- does not flash them.
            Item {
                anchors.fill: parent
                visible: panel.manager.busy && skeletonDelay.triggered

                Timer {
                    id: skeletonDelay
                    property bool triggered: false
                    interval: 250
                    running: panel.manager.busy
                    onTriggered: triggered = true
                }
                Connections {
                    target: panel.manager
                    function onBusyChanged() {
                        if (!panel.manager.busy)
                            skeletonDelay.triggered = false
                    }
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.space.md
                    spacing: Theme.space.md

                    Repeater {
                        model: 9

                        Row {
                            required property int index
                            spacing: Theme.space.lg
                            opacity: 0.55

                            SequentialAnimation on opacity {
                                loops: Animation.Infinite
                                running: Theme.motion.base > 0
                                NumberAnimation { to: 0.30; duration: 800 }
                                NumberAnimation { to: 0.55; duration: 800 }
                            }

                            Rectangle {
                                width: 74
                                height: 11
                                radius: Theme.radius.sm
                                color: Theme.color.bgHover
                            }
                            Rectangle {
                                // Varied widths so it reads as text rather than
                                // as a bar chart. Deterministic per row: a
                                // random width would flicker on every repaint.
                                width: 90 + ((index * 37) % 110)
                                height: 11
                                radius: Theme.radius.sm
                                color: Theme.color.bgHover
                            }
                        }
                    }
                }
            }

            // Determinate progress. Nine seconds on the 65-track film is long
            // enough that a percentage alone reads as stalled.
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                width: parent.width * (panel.manager.progress / 100)
                height: 2
                visible: panel.manager.busy
                color: Theme.color.accent
            }
        }
    }

    // ---- menus -------------------------------------------------------------
    AppMenu {
        id: trackMenu
        implicitWidth: 320
        maximumHeight: 460

        Repeater {
            model: panel.manager.tracks

            AppMenuItem {
                required property var modelData
                required property int index
                text: modelData.label + "  ·  " + modelData.lineCount + " lines"
                enabled: modelData.browsable
                checkable: true
                checked: index === panel.ui.tabIndex
                onTriggered: {
                    panel.ui.tabIndex = index
                    panel.trackActivated(index)
                }
            }
        }
    }

    AppMenu {
        id: moreMenu
        preferAbove: false

        AppMenuItem {
            text: panel.detached ? "Dock the browser" : "Detach into its own window"
            iconName: panel.detached ? "dock-right" : "external-link"
            shortcutText: panel.shortcuts.sequenceFor("panel-detach")
            onTriggered: panel.detachToggled()
        }

        AppMenuItem {
            text: "Export this track as .srt…"
            iconName: "download"
            // Nothing to write before a track with lines is selected, and a save
            // dialog that produces an empty file is worse than a disabled entry.
            enabled: panel.linesModel.sourceCount > 0
            shortcutText: panel.shortcuts.sequenceFor("export-track")
            onTriggered: panel.exportRequested()
        }

        AppMenuItem {
            text: "Open a subtitle file…"
            iconName: "folder-open"
            shortcutText: panel.shortcuts.sequenceFor("open-subtitle")
            onTriggered: panel.openSubtitleRequested()
        }

        AppMenuSeparator {}

        AppMenuItem {
            text: "Adjust subtitle timing"
            iconName: "clock-arrows"
            checkable: true
            checked: panel.ui.syncBarVisible
            onTriggered: panel.ui.syncBarVisible = !panel.ui.syncBarVisible
        }

        AppMenuSeparator {}

        AppMenuItem {
            text: Theme.dark ? "Light theme" : "Dark theme"
            iconName: "type"
            onTriggered: Theme.dark = !Theme.dark
        }

        AppMenuItem {
            text: "Show subtitle styling"
            checkable: true
            checked: Theme.showStyling
            onTriggered: Theme.showStyling = !Theme.showStyling
        }

        AppMenuItem {
            text: "Larger text"
            iconName: "plus"
            enabled: Theme.rowFontSize < Theme.maximumRowFontSize
            shortcutText: panel.shortcuts.sequenceFor("row-larger")
            onTriggered: Theme.rowFontSize = Theme.rowFontSize + 1
        }

        AppMenuItem {
            text: "Smaller text"
            iconName: "minus"
            enabled: Theme.rowFontSize > Theme.minimumRowFontSize
            shortcutText: panel.shortcuts.sequenceFor("row-smaller")
            onTriggered: Theme.rowFontSize = Theme.rowFontSize - 1
        }

        AppMenuSeparator {}

        AppMenuItem {
            text: "Settings…"
            iconName: "settings"
            shortcutText: panel.shortcuts.sequenceFor("settings")
            onTriggered: panel.settingsRequested()
        }
    }
}
