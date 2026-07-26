// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

pragma Singleton

import QtQuick

// Every icon in the player, as SVG path data on a 24x24 grid.
//
// Path data in a singleton rather than image files, an icon font, or a CDN.
// Nothing to load, nothing to decode, nothing to ship alongside the binary,
// crisp at any size and device pixel ratio, and tinted by setting one colour
// rather than by a colorize pass. That last point matters here: the app runs on
// a software rasterizer often enough that an extra render pass per icon would
// be a real cost.
//
// It also settles a question the transport bar got wrong. Every control was
// labelled in prose -- "Play", "Vol", "Subs", "Full", and typographic
// guillemets standing in for previous/next -- with a comment explaining that WSL
// images ship no emoji font. That is true, and the conclusion drawn from it was
// wrong: the answer to "no emoji font" is vector icons, not words.
//
// Each glyph is an object with up to two components:
//   f  a filled path, drawn with the icon's colour
//   s  a stroked path, 2px on the 24px grid, round caps and joins
// Most icons use one or the other; the volume icons use both, because a filled
// speaker with stroked arcs is what reads correctly at 16px.
//
// Geometry follows the Lucide/Feather conventions (ISC-licensed set, 24x24,
// 2px stroke) but the paths here are authored for this project.
QtObject {
    readonly property var glyphs: ({
        // ---- transport, filled: solid shapes read better at 16px ----
        "play":          { f: "M8 5.1v13.8L19 12z" },
        "pause":         { f: "M6.5 4.6h3.6v14.8H6.5zM13.9 4.6h3.6v14.8h-3.6z" },
        "stop":          { f: "M6.5 6.5h11v11h-11z" },
        "skip-back":     { f: "M18.5 5.2v13.6L8.6 12zM5.5 5.2h2.6v13.6H5.5z" },
        "skip-forward":  { f: "M5.5 5.2v13.6L15.4 12zM15.9 5.2h2.6v13.6h-2.6z" },

        // ---- volume: filled cone, stroked waves ----
        "volume-high":   { f: "M11 4.8 5.8 9H2.4v6h3.4L11 19.2z",
                           s: "M14.8 9.2a4 4 0 0 1 0 5.6M17.6 6.4a8 8 0 0 1 0 11.2" },
        "volume-medium": { f: "M11 4.8 5.8 9H2.4v6h3.4L11 19.2z",
                           s: "M14.8 9.2a4 4 0 0 1 0 5.6" },
        "volume-low":    { f: "M11 4.8 5.8 9H2.4v6h3.4L11 19.2z" },
        "volume-mute":   { f: "M11 4.8 5.8 9H2.4v6h3.4L11 19.2z",
                           s: "M15.5 9.5l6 5M21.5 9.5l-6 5" },

        // ---- window ----
        "fullscreen-enter": { s: "M3 8.5V4h4.5M16.5 4H21v4.5M21 15.5V20h-4.5M7.5 20H3v-4.5" },
        "fullscreen-exit":  { s: "M7.5 3.5V8H3M21 8h-4.5V3.5M16.5 20.5V16H21M3 16h4.5v4.5" },

        // ---- tracks ----
        "subtitles":     { s: "M4 5.5h16a1.5 1.5 0 0 1 1.5 1.5v10a1.5 1.5 0 0 1-1.5 1.5H4A1.5 1.5 0 0 1 2.5 17V7A1.5 1.5 0 0 1 4 5.5zM6 10.5h3.5M12 10.5h6M6 14h6M14.5 14h3.5" },
        "subtitles-off": { s: "M4 5.5h16a1.5 1.5 0 0 1 1.5 1.5v10a1.5 1.5 0 0 1-1.5 1.5H4A1.5 1.5 0 0 1 2.5 17V7A1.5 1.5 0 0 1 4 5.5zM3 3l18 18" },
        "audio-track":   { s: "M9 18.2V6.4l10-2v11.8M9 18.2a2.4 2.4 0 1 1-4.8 0 2.4 2.4 0 0 1 4.8 0zM19 16.2a2.4 2.4 0 1 1-4.8 0 2.4 2.4 0 0 1 4.8 0z" },

        // ---- panel ----
        "external-link": { s: "M14 4h6v6M20 4l-8.5 8.5M18 13.5V19a1.5 1.5 0 0 1-1.5 1.5h-11A1.5 1.5 0 0 1 4 19V8a1.5 1.5 0 0 1 1.5-1.5H11" },
        "dock-right":    { s: "M3.5 5h17a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1h-17a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1zM14.5 5v14" },
        "download":      { s: "M12 3.5v11M7.5 10.5 12 15l4.5-4.5M4 19h16" },
        // Follow: a reticle. The list locking onto the line being spoken.
        "crosshair-lock": { s: "M12 3.5v3.2M12 17.3v3.2M3.5 12h3.2M17.3 12h3.2M12 7.4a4.6 4.6 0 1 0 0 9.2 4.6 4.6 0 0 0 0-9.2z" },

        // ---- chrome ----
        "search":        { s: "M10.8 4a6.8 6.8 0 1 0 0 13.6 6.8 6.8 0 0 0 0-13.6zM15.8 15.8 20.5 20.5" },
        "close":         { s: "M6 6l12 12M18 6 6 18" },
        "check":         { s: "M5 12.6 9.5 17.1 19 6.9" },
        "chevron-left":  { s: "M15 4.5 8 12l7 7.5" },
        "chevron-right": { s: "M9 4.5 16 12l-7 7.5" },
        "chevron-down":  { s: "M4.5 9 12 16l7.5-7" },
        "chevron-up":    { s: "M4.5 15 12 8l7.5 7" },
        "more-vertical": { f: "M12 5.4a1.85 1.85 0 1 0 0 3.7 1.85 1.85 0 0 0 0-3.7zM12 10.15a1.85 1.85 0 1 0 0 3.7 1.85 1.85 0 0 0 0-3.7zM12 14.9a1.85 1.85 0 1 0 0 3.7 1.85 1.85 0 0 0 0-3.7z" },
        "grip-vertical": { f: "M9.4 5.2a1.6 1.6 0 1 0 0 3.2 1.6 1.6 0 0 0 0-3.2zM9.4 10.4a1.6 1.6 0 1 0 0 3.2 1.6 1.6 0 0 0 0-3.2zM9.4 15.6a1.6 1.6 0 1 0 0 3.2 1.6 1.6 0 0 0 0-3.2zM14.6 5.2a1.6 1.6 0 1 0 0 3.2 1.6 1.6 0 0 0 0-3.2zM14.6 10.4a1.6 1.6 0 1 0 0 3.2 1.6 1.6 0 0 0 0-3.2zM14.6 15.6a1.6 1.6 0 1 0 0 3.2 1.6 1.6 0 0 0 0-3.2z" },
        "plus":          { s: "M12 5v14M5 12h14" },
        "minus":         { s: "M5 12h14" },

        // ---- features ----
        "settings":      { s: "M4 6.5h8M16 6.5h4M4 12h4M12 12h8M4 17.5h9M17 17.5h3M14 4.3v4.4M10 9.8v4.4M15 15.3v4.4" },
        "gauge":         { s: "M3.6 17.5a9 9 0 1 1 16.8 0M12 12.4l4.4-4" },
        "repeat":        { s: "M4 9.2V7.6A2.6 2.6 0 0 1 6.6 5H20M16.8 2 20 5l-3.2 3M20 14.8v1.6a2.6 2.6 0 0 1-2.6 2.6H4M7.2 22 4 19l3.2-3" },
        "camera":        { s: "M4 8.5h3.2l1.5-2.6h6.6l1.5 2.6H20a1.5 1.5 0 0 1 1.5 1.5v8a1.5 1.5 0 0 1-1.5 1.5H4A1.5 1.5 0 0 1 2.5 18v-8A1.5 1.5 0 0 1 4 8.5zM12 10.6a3.6 3.6 0 1 0 0 7.2 3.6 3.6 0 0 0 0-7.2z" },
        "list-video":    { s: "M3 6.5h9M3 12h9M3 17.5h6",
                           f: "M15.4 9.6v8.8l7-4.4z" },
        "folder-open":   { s: "M3.5 18.5V6.6a1 1 0 0 1 1-1H9l2.2 2.6h8.3a1 1 0 0 1 1 1v2.3M3.5 18.5l2.9-7h15.1l-2.9 7z" },
        // Subtitle sync: a clock, because what is being moved is time.
        "clock-arrows":  { s: "M12 4.4a7.6 7.6 0 1 0 0 15.2 7.6 7.6 0 0 0 0-15.2zM12 8.2v4.2l2.9 1.9" },
        "type":          { s: "M5 6.6V5h14v1.6M12 5.2v13.6M9.2 18.8h5.6" },
        "film":          { s: "M3.5 4.5h17a1 1 0 0 1 1 1v13a1 1 0 0 1-1 1h-17a1 1 0 0 1-1-1v-13a1 1 0 0 1 1-1zM7.6 4.6v14.8M16.4 4.6v14.8M2.6 12h18.8M2.6 8.3h5M2.6 15.7h5M16.4 8.3h5M16.4 15.7h5" },
        "keyboard":      { s: "M3.5 6.5h17a1 1 0 0 1 1 1v9a1 1 0 0 1-1 1h-17a1 1 0 0 1-1-1v-9a1 1 0 0 1 1-1zM8 14h8",
                           f: "M6.3 9.4h1.5v1.5H6.3zM10 9.4h1.5v1.5H10zM13.7 9.4h1.5v1.5h-1.5zM17.4 9.4h1.5v1.5h-1.5z" },

        // ---- status ----
        "alert-triangle": { s: "M12 4.2 21.2 19.8H2.8zM12 10.2v3.9",
                            f: "M12 16.1a1.05 1.05 0 1 0 0 2.1 1.05 1.05 0 0 0 0-2.1z" },
        "info":           { s: "M12 3.5a8.5 8.5 0 1 0 0 17 8.5 8.5 0 0 0 0-17zM12 11.3v5.4",
                            f: "M12 7.1a1.15 1.15 0 1 0 0 2.3 1.15 1.15 0 0 0 0-2.3z" }
    })

    // An unknown name draws nothing rather than throwing, so a typo is a missing
    // icon and not a broken window.
    function has(name) { return glyphs[name] !== undefined }
}
