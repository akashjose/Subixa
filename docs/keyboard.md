# Keyboard and controls

> Defaults only. ShortcutRegistry::actions() is the source of truth and Settings -> Hotkeys is the UI.
> Split out of `CLAUDE.md`, which is the entry point and links here.

## Defaults

**Every binding lives in `ShortcutRegistry`, not in QML.** The table there carries an id, a
label, a category, a default sequence, and whether the binding still works while the search
box has focus. `Main.qml` repeats over it to create the `Shortcut` objects and dispatches by
id; menus and tooltips read their key caps from the same table. That replaced 125 lines of
hardcoded `Shortcut` elements which could express a fixed set of bindings and nothing else —
no remapping, no way to show a shortcut next to a menu item without writing the string out
again by hand, and no way to notice that two things wanted the same key.

Overrides live in the `[hotkeys]` group. Rebinding to the default *clears* the override
rather than storing a copy of it, so a later change to a default still reaches anyone who
once touched that row. `tst_shortcuts` covers that, plus conflict detection, unbinding as
distinct from resetting, and that no two actions ship with the same default.

The defaults, abbreviated — the full table is in `ShortcutRegistry::actions()` and in
Settings → Hotkeys:

| | |
|---|---|
| Space, K | play/pause |
| ← / → | seek, Shift for 1 s; step size is a setting |
| J / L | large seek |
| Ctrl+← / Ctrl+→ | previous / next subtitle line |
| ↑ / ↓ | volume, M mutes |
| F, F11 | fullscreen, Esc leaves it |
| Tab | show/hide the subtitle panel |
| Ctrl+D | detach the panel into its own window, or dock it again |
| Ctrl+F | focus the search box |
| Ctrl+O / Ctrl+Shift+O | open a file / open a subtitle file |
| [ / ] | playback speed, Backspace resets |
| Ctrl+[ / Ctrl+] | subtitle delay, Ctrl+0 resets |
| Ctrl+Shift+S | sync subtitles to the selected line |
| A / B / Shift+A | set loop start / end / clear |
| R | loop the line playing now |
| V | subtitles on the video on/off |
| Ctrl+C / Ctrl+Shift+C | copy the current line, with or without its timestamp |
| Ctrl+S | screenshot |
| Ctrl+, | settings |
| < / > | previous/next file in the queue |
| Ctrl+= / Ctrl+- | subtitle row text size, remembered |

Guarding is per action rather than blanket: a binding is disabled while the search box has
focus unless its `worksWhileTyping` flag says the sequence carries a modifier a `TextField`
does not claim. Without that, typing "film" into the search box toggled fullscreen and mute.

Anything reached by key is also reachable by mouse. The transport's overflow menu (the
rightmost button, always visible) holds whatever the width breakpoints have dropped plus the
things that have no button of their own, so nothing is keyboard-only.
