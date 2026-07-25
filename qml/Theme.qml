pragma Singleton

import QtQuick

// Every colour in the player, in one place, in two schemes.
//
// A singleton rather than properties threaded down through the panel: the panel
// is destroyed and rebuilt every time it is docked or detached (see
// SubtitlePanel.qml), so anything it held would have to be restored, and the two
// windows would have to be kept in step by hand.
//
// `dark` and `rowFontSize` are plain properties, not readonly bindings: the
// window restores them from the saved preferences at startup and writes them
// back when they change. Everything below is derived, so one assignment restyles
// both windows.
QtObject {
    id: theme

    property bool dark: true
    // Row text in the browser. Someone reading along with a film wants this
    // larger than a UI font; someone scanning a whole track wants more rows.
    property int rowFontSize: 12
    // Whether the browser renders the subtitler's own italics, bold and speaker
    // colours, or flattens every row to plain text. On by default: those carry
    // meaning, and throwing them away was the one thing the list did that the
    // picture did not.
    property bool showStyling: true

    readonly property int minimumRowFontSize: 9
    readonly property int maximumRowFontSize: 22

    // The picture's surround stays black in both schemes: it is the letterbox
    // area, and a light grey border around a film is nobody's idea of a theme.
    readonly property color videoBackground: "black"

    readonly property color windowBackground: dark ? "#0e0e12" : "#eceef2"
    readonly property color transportBackground: dark ? "#16161d" : "#dfe2e8"
    readonly property color panelBackground: dark ? "#12121a" : "#f6f7f9"
    readonly property color panelHeader: dark ? "#1b1b25" : "#e4e6ec"

    readonly property color text: dark ? "#d0d0dc" : "#20222a"
    readonly property color textStrong: dark ? "#ffffff" : "#000000"
    readonly property color textDim: dark ? "#6a6a7a" : "#6b6e7a"
    readonly property color textMuted: dark ? "#9a9aa8" : "#5c606b"

    readonly property color timestamp: dark ? "#6f6f85" : "#7b7e8c"
    readonly property color timestampCurrent: dark ? "#9fb6dc" : "#28497c"

    readonly property color accent: dark ? "#42618f" : "#4a76bd"
    readonly property color currentRow: dark ? "#23324a" : "#d3e0f5"
    readonly property color hoverRow: dark ? "#1b1b25" : "#e6eaf2"
    readonly property color busy: dark ? "#c8a45c" : "#8a6516"

    readonly property color splitHandle: dark ? "#1b1b25" : "#d5d8de"
    readonly property color splitHandleHover: dark ? "#2c2c3a" : "#c3c7d0"
    readonly property color splitGrip: dark ? "#4a4a5c" : "#9aa0ac"

    // Drawn over the video, so both need enough alpha to stay legible against
    // whatever frame is underneath.
    readonly property color overlayBackground: dark ? "#cc1c1c24" : "#e6ffffff"
    readonly property color overlayBorder: dark ? "#3a3a48" : "#b9bec9"
    readonly property color overlayText: dark ? "#c8c8d4" : "#2a2c34"

    readonly property color errorBackground: dark ? "#e6521f27" : "#f2f6d5d8"
    readonly property color errorBorder: dark ? "#8c3038" : "#c98d94"
    readonly property color errorText: dark ? "#ffdde0" : "#4a1418"
}
