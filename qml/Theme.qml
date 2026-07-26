// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

pragma Singleton

import QtQuick

// The design system: every colour, size, radius, duration and type step in the
// player, in one place, in two schemes.
//
// A singleton rather than properties threaded down through the panel: the panel
// is destroyed and rebuilt every time it is docked or detached (see
// SubtitlePanel.qml), so anything it held would have to be restored, and the two
// windows would have to be kept in step by hand.
//
// Tokens are grouped -- Theme.color.textPrimary, Theme.space.lg -- rather than
// flat. The flat list this replaced had 24 colours and nothing else, so every
// component invented its own padding, radius and font size inline; nothing lined
// up because nothing was derived from anything.
//
// Contrast is a promise, not an aspiration. Every text token below is annotated
// with its measured WCAG 2.1 ratio against the surfaces it is allowed on, and
// body text clears 4.5:1 everywhere it is permitted. The palette this replaced
// failed that in eight places, including the parse-progress readout and -- worst
// -- the timestamp on the currently-playing row, which the highlight made
// *harder* to read than an ordinary row.
QtObject {
    id: t

    // ---- mutable, restored from QSettings by Main.qml ------------------
    property bool dark: true
    // Row text in the browser. Someone reading along with a film wants this
    // larger than a UI font; someone scanning a whole track wants more rows.
    property int rowFontSize: 13
    // Whether the browser renders the subtitler's own italics, bold and speaker
    // colours, or flattens every row to plain text. On by default: those carry
    // meaning, and throwing them away was the one thing the list did that the
    // picture did not.
    property bool showStyling: true
    property bool reducedMotion: false
    // Set false on Mesa's software rasterizers, where a shadow costs a render
    // pass the driver can ill afford. Every elevation level therefore carries a
    // distinct surface colour and border as well as a shadow -- the UI has to be
    // complete without a single one of them.
    property bool effectsEnabled: true
    // Draw text with QPainter into a texture rather than with the scene graph's
    // glyph materials. Set from the graphics driver at startup and overridable
    // in Settings -> Interface; see PaintedText.h. Off is the fast, sharp,
    // correct-everywhere default.
    property bool paintedText: false

    readonly property int minimumRowFontSize: 9
    readonly property int maximumRowFontSize: 22

    // The picture's surround stays black in both schemes: it is the letterbox
    // area, and a light grey border around a film is nobody's idea of a theme.
    readonly property color videoBackground: "black"

    // ---- colour --------------------------------------------------------
    readonly property QtObject color: QtObject {
        // Surfaces. Near-neutral with a slight blue cast (hue ~225, 15-20%
        // saturation): pure grey reads cheap, strongly tinted reads like a skin.
        readonly property color bgBase: t.dark ? "#0b0d12" : "#eef0f4"
        readonly property color bgSurface: t.dark ? "#12151c" : "#ffffff"
        readonly property color bgRaised: t.dark ? "#191d26" : "#f5f6f9"
        readonly property color bgOverlay: t.dark ? "#1f242f" : "#ffffff"
        readonly property color bgSunken: t.dark ? "#090b0f" : "#e4e7ec"
        readonly property color bgHover: t.dark ? "#1a1e27" : "#f1f3f7"
        readonly property color bgPressed: t.dark ? "#232833" : "#e6e9ef"
        readonly property color bgSelected: t.dark ? "#1c2740" : "#dce8fb"
        readonly property color bgSelectedHover: t.dark ? "#223050" : "#cddef8"

        // Drawn over video, so both need enough alpha to stay legible against
        // whatever frame is underneath.
        readonly property color scrimVideo: "#b3000000"
        readonly property color scrimModal: t.dark ? "#99000000" : "#66000000"

        // Borders. borderFocus is 5.63:1 on dark and carries the keyboard focus
        // ring, which has to clear 3:1 as a non-text indicator.
        readonly property color borderSubtle: t.dark ? "#1e232d" : "#e5e8ee"
        readonly property color border: t.dark ? "#2a303c" : "#d2d7e0"
        readonly property color borderStrong: t.dark ? "#3a4150" : "#b4bcc9"
        readonly property color borderFocus: t.dark ? "#4d8df7" : "#1f6feb"

        // Text. Dark ratios against bgSurface / bgRaised / bgSelected:
        //   textPrimary   15.18 / 14.02 / 12.35
        //   textSecondary  8.57 /  7.92 /  6.97
        //   textTertiary   4.92 /  4.54 /  4.00
        // textTertiary is therefore permitted on bgSurface and bgRaised only.
        // On bgSelected and bgOverlay it falls below 4.5 and callers must step
        // up to textSecondary -- the subtitle row delegate is the case that
        // matters and it does exactly that.
        readonly property color textPrimary: t.dark ? "#e8eaf0" : "#161a22"
        readonly property color textSecondary: t.dark ? "#aab2c2" : "#4a5263"
        readonly property color textTertiary: t.dark ? "#7c8596" : "#626a7c"
        // Deliberately below 4.5. WCAG exempts disabled controls, and a disabled
        // control that does not read as disabled is the worse failure.
        readonly property color textDisabled: t.dark ? "#525a6b" : "#9aa1b0"

        // Accent. 5.63:1 on dark against bgSurface, so it can carry a focus
        // ring, a progress fill and a selection marker -- none of which the old
        // desaturated navy could do at 2.96.
        readonly property color accent: t.dark ? "#4d8df7" : "#1f6feb"
        readonly property color accentHover: t.dark ? "#6ba1ff" : "#1a5fcc"
        readonly property color accentPressed: t.dark ? "#3c79dc" : "#14509f"
        readonly property color accentDisabled: t.dark ? "#2b3a57" : "#b9cdf0"
        readonly property color accentSubtle: t.dark ? "#1b283f" : "#e0ebfc"
        // Accent as *text*. On dark the fill colour is legible enough to double
        // as text; on light it is not (4.06 on bgBase), so the two part company
        // and callers never have to branch on the scheme themselves.
        readonly property color accentText: t.dark ? "#4d8df7" : "#1a5fcc"
        // Near-black on a bright accent, not white: white on #4d8df7 is 3.25:1.
        readonly property color onAccent: t.dark ? "#06090f" : "#ffffff"

        // Semantic
        readonly property color success: t.dark ? "#3fc98a" : "#127a44"
        readonly property color successBg: t.dark ? "#19322e" : "#e3efe9"
        readonly property color successText: t.dark ? "#bdf0d8" : "#0a4527"
        readonly property color warning: t.dark ? "#e3a94b" : "#8a5a00"
        readonly property color warningBg: t.dark ? "#332d24" : "#f4ede0"
        readonly property color warningText: t.dark ? "#f7e2be" : "#4a3000"
        readonly property color error: t.dark ? "#f2666b" : "#c1272d"
        readonly property color errorBg: t.dark ? "#362229" : "#f8e5e6"
        readonly property color errorText: t.dark ? "#ffd9db" : "#7a1518"
        readonly property color info: t.dark ? "#4d8df7" : "#1a5fcc"
        readonly property color infoBg: t.dark ? "#1b283f" : "#e4edfb"
        readonly property color infoText: t.dark ? "#c8dcff" : "#123f87"

        // The subtitle browser. Both timestamp tokens clear 4.5:1 on the
        // *current* row as well as an ordinary one, which is the specific thing
        // the old palette got backwards.
        //   dark:  timestamp 5.85 on bgSurface, 4.76 on bgSelected
        //          timestampCurrent 9.86 / 8.02
        //   light: timestamp 6.08 / 4.83, timestampCurrent 8.23 / 6.53
        readonly property color timestamp: t.dark ? "#7e93b5" : "#55637a"
        readonly property color timestampCurrent: t.dark ? "#9fc0f5" : "#1f4c99"
        // The 3px rail down the left of the playing row. This carries the
        // signal; the row fill only carries the context.
        readonly property color cueMarker: t.dark ? "#4d8df7" : "#1f6feb"
        readonly property color searchHit: t.dark ? "#26344c" : "#cfe0fa"

        readonly property color splitHandle: t.dark ? "#1e232d" : "#e5e8ee"
        readonly property color splitHandleHover: t.dark ? "#3a4150" : "#b4bcc9"

        // ---- deliberately not scheme-aware --------------------------------
        // The two groups below are the only colours in the app that do not
        // follow `dark`, and both for the same reason: they are drawn on
        // something whose colour is not ours to know.

        // Knobs. A slider thumb sits on an accent fill at one end of its travel
        // and on a sunken groove at the other, and a switch thumb sits on an
        // accent fill or a grey one. White with a soft dark edge is the only
        // pair that reads on all four, which is why every platform lands on it.
        readonly property color knob: "#ffffff"
        readonly property color knobEdge: "#33000000"
        // The ring around a colour tile in the subtitle-appearance picker. The
        // tile holds a user-chosen colour, so the ring cannot assume anything
        // about it -- including that it is not the surface behind it.
        readonly property color tileEdge: "#55000000"

        // Chrome over the picture, in fullscreen. White and translucent black,
        // never the theme's own text colours: these sit on arbitrary film
        // frames, so a light theme must not make them dark and unreadable over
        // a night scene.
        readonly property color onVideo: "#ffffff"
        readonly property color onVideoMuted: "#b3ffffff"
        readonly property color onVideoMark: "#99ffffff"
        readonly property color onVideoTrack: "#4dffffff"
        readonly property color onVideoBuffer: "#33ffffff"
    }

    // ---- spacing, radius, borders --------------------------------------
    // A 4px base, with a 2 and a 6 for dense controls. Control-internal padding
    // draws from xs..lg, gaps between related controls md, between groups xl.
    readonly property QtObject space: QtObject {
        readonly property int xxs: 2
        readonly property int xs: 4
        readonly property int sm: 6
        readonly property int md: 8
        readonly property int lg: 12
        readonly property int xl: 16
        readonly property int xxl: 20
        readonly property int xxxl: 24
        readonly property int huge: 32
    }

    readonly property QtObject radius: QtObject {
        readonly property int none: 0
        readonly property int sm: 4    // chips, badges, inline highlights
        readonly property int md: 6    // buttons, fields, menu items, rows
        readonly property int lg: 10   // panels, popovers, menus, banners
        readonly property int xl: 14   // dialogs
        readonly property int pill: 999
    }

    readonly property QtObject stroke: QtObject {
        readonly property int hairline: 1
        readonly property int focus: 2
        readonly property int marker: 3   // the current-cue rail
        readonly property int track: 4    // seek/volume groove, idle
        readonly property int trackHover: 6
    }

    // Minimum hit targets. The control this replaced put the entry point to six
    // features behind a 10px label with 4px padding -- a 34x18 target.
    readonly property QtObject size: QtObject {
        readonly property int iconButtonSm: 28
        readonly property int iconButton: 32
        readonly property int iconButtonLg: 36
        readonly property int rowMin: 28
        readonly property int fieldHeight: 28
        readonly property int transportHeight: 48
        readonly property int seekStripHeight: 20
        readonly property int panelHeaderHeight: 40
    }

    // ---- type ----------------------------------------------------------
    // Fonts are resolved once, here, against what the machine actually has.
    //
    // A `font.families` fallback list would be the natural way to express this,
    // but Qt 6.9's QML font value type does not expose `families` -- only
    // `family` -- so a list assigned there fails at load with an error that
    // points at the property rather than at the missing feature. Walking the
    // preference list against Qt.fontFamilies() gives the same result and works
    // everywhere.
    //
    // The point of the list at all: the platform default here is DejaVu Sans, a
    // 2004 Bitstream Vera derivative whose wide, low-contrast letterforms are
    // one of the strongest "old Linux application" signals in a screenshot.
    //
    // Sizes are pixelSize throughout: pointSize varies with reported DPI and
    // WSLg misreports it.
    function pickFont(candidates, fallback) {
        var available = Qt.fontFamilies()
        for (var i = 0; i < candidates.length; ++i) {
            if (available.indexOf(candidates[i]) >= 0)
                return candidates[i]
        }
        return fallback
    }

    readonly property QtObject type: QtObject {
        readonly property string sans: t.pickFont(
            ["Inter", "Segoe UI Variable Text", "Segoe UI", "Ubuntu Sans",
             "Noto Sans", "DejaVu Sans"], "sans-serif")
        // Timestamps are the column the eye scans down a 93 000-row list, so
        // they get real tabular figures rather than a proportional face with
        // digits of different widths.
        readonly property string mono: t.pickFont(
            ["JetBrains Mono", "IBM Plex Mono", "Cascadia Mono",
             "Ubuntu Sans Mono", "Noto Sans Mono", "DejaVu Sans Mono"],
            "monospace")

        readonly property int displaySize: 24
        readonly property int titleSize: 17
        readonly property int headingSize: 14
        readonly property int bodySize: 13
        readonly property int labelSize: 12
        readonly property int captionSize: 11
        readonly property int overlineSize: 10
        readonly property int monoSize: 12

        // Only 400 and 600 are used. Never font.bold -- a weight is predictable
        // when the resolved family lacks it, a synthesised bold is not.
        readonly property int weightNormal: Font.Normal
        readonly property int weightMedium: Font.Medium
        readonly property int weightStrong: Font.DemiBold

        readonly property real bodyLine: 1.38
        readonly property real cueLine: 1.45
    }

    // ---- motion --------------------------------------------------------
    // Every one of these is a property animation on colour, opacity or
    // position -- no blur, no shader, no particle. Safe on llvmpipe.
    readonly property QtObject motion: QtObject {
        readonly property int fast: t.reducedMotion ? 0 : 110
        readonly property int base: t.reducedMotion ? 0 : 170
        readonly property int slow: t.reducedMotion ? 0 : 260
        // The subtitle list's auto-scroll. Kept at the value the band-following
        // behaviour was tuned against.
        readonly property int follow: t.reducedMotion ? 0 : 220
        // The current-row crossfade. Below ~100ms it strobes on rapid dialogue;
        // above ~200ms it lags the audio.
        readonly property int cueFade: t.reducedMotion ? 0 : 140
        readonly property int standard: Easing.OutCubic
        readonly property int emphasis: Easing.InOutQuad
    }

    // ---- elevation -----------------------------------------------------
    // Surface + border + optional shadow. The surface and border alone must
    // distinguish every level, because the shadow is gated on effectsEnabled.
    readonly property QtObject elevation: QtObject {
        readonly property color shadowLow: t.dark ? "#59000000" : "#1f000000"
        readonly property color shadowMid: t.dark ? "#73000000" : "#29000000"
        readonly property color shadowHigh: t.dark ? "#8c000000" : "#33000000"
    }
}
