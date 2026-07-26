# Testing, and how to see the UI

> The suites, the render canary, and the Windows-side tooling that makes a screenshot trustworthy.
> Split out of `CLAUDE.md`, which is the entry point and links here.

## Tests

```bash
cd build && ctest --output-on-failure     # or run ./build/tst_subtitles directly
```

Six headless suites, none needing a compositor or a rendered frame —
deliberately, because a screenshot is the least reliable evidence available here
(see the degraded-state note below). Run these before reaching for the UI.

- **`tst_subtitles`** — the extractor against the fixtures (the 8-comma ASS field
  layout, entity decoding, both halves of the rebase rule) plus
  `SubtitleLineModel::indexAt()`, the filter, and `rowAt()`/`startMsAt()` mapping.
  Also the cue cache (a hit has to reproduce a parse exactly; a changed mtime, a
  new sidecar or a truncated entry has to miss), the `.srt` export (checked by
  parsing back what it wrote), and the styling: tag handling, escaping, dropped
  vector drawings, and that an adjusted cue colour still clears 4.5:1 against the
  row it lands on.
  Sources are compiled into the test target rather than shared via a static
  library: `qt_add_qml_module` registers the QML-exposed types from the sources
  listed in `qt_add_executable`, and moving them out breaks that registration.
- **`tst_mpvtracks`** — libmpv directly, `vo=null`. Verifies that selecting a
  track changes what mpv *would* burn over the video, read back as text from
  `sub-text` rather than looked at. `track-list/N/ff-index` is confirmed present,
  which is what lets a panel tab address an mpv track without matching language
  strings. Note `vo=null` rather than `QT_QPA_PLATFORM=offscreen`: offscreen never
  creates a render context, so the queued `loadfile` never flushes and mpv loads
  nothing at all (trap 2).
- **`tst_qmlpanel`** — the QML layer, which until now had no harness at all. It
  loads the real `Main.qml` (not a mock) under `QT_QPA_PLATFORM=offscreen` and
  drives it through the object tree: a tab click swaps the model, the search box
  reaches the proxy after its debounce, `currentRow` scrolls the view and stops
  when follow is off, a remembered track is restored on the next open, detaching
  keeps search text and tab, and the theme singleton reaches a panel in either
  window. Nothing in it asserts a pixel. It needs `offscreen` rather than
  `minimal` — `minimal` has no scene graph, so Loaders never instantiate and the
  panel never exists. It writes into a temporary `XDG_CONFIG_HOME`/`XDG_CACHE_HOME`,
  since the player now remembers things and a test that quietly wrote into the
  developer's own settings would be the exact bug this suite exists to catch.

  It earned its keep on the first run, twice. It caught detaching the panel
  silently resetting the reader's tab (trap 13), and then caught an
  `onOpened` handler on `FileDialog` that does not exist — a QML error that
  would have reached a screenshot, not a compiler.

- **`tst_playlist`** — what plays next, which is pure file-system reasoning and so
  needs neither mpv nor a window. Mostly ordering (`ep2` before `ep10`, padded
  and unpadded spellings of the same number adjacent) and boundaries: the last
  file does not wrap to the first, a drop keeps the order it was dropped in with
  duplicates collapsed, sidecars and posters are not things to play next, and the
  file being opened is queued even when its extension is not on the list. The QML
  harness covers the other half — that opening a file queues its folder and that
  advancing does not rebuild the queue.

- **`tst_shortcuts`** — the keyboard table and its overrides, against a temporary
  ini file. Mostly policy: an override survives a reopen, rebinding to the
  default *clears* the override rather than storing a copy (so a later change to
  a default still reaches anyone who once touched that row), two actions cannot
  silently claim one key, `ctrl+d` and `Ctrl+D` are one binding rather than two,
  and unbinding is distinct from resetting. It also asserts that no two actions
  ship with the same default, which is the check that stops the table rotting as
  it grows.

- **`tst_playbackhistory`** — the resume-position store against a temporary ini file,
  and mostly its policy: too short, barely started, and near-the-end all mean *do
  not resume*, and finishing a file clears a position saved earlier. Also that two
  films with the same basename in different directories do not collide — paths are
  hashed because `QSettings` reads `/` as a group separator — and that the same file
  named relatively and absolutely resolves to one entry. Since the same store now
  also remembers the subtitle track being read, it covers that the two are kept in
  separate groups: finishing a film clears the position and must *not* forget that
  this household reads the Latin American Spanish track.

`tst_subtitles` also now covers two things that were bugs rather than
hypotheticals: that a poisoned count in a cache entry reads back as a miss with
the real cues still returned (see the note in the test about what that does and
does not prove), and that the styled and plain text of every cue in every fixture
*render to the same characters*. The second is the invariant the whole panel
rests on — search matches the plain text — and it was false: entities were
decoded on the way to `text` but not on the way to `styled`, so with styling on
(the default) the browser showed "a &amp;amp; entity" while search matched
"a & entity". `SubtitleText` now serves both paths.

The harness found one real bug on its first run, since fixed: search did not fold
U+00A0 to a plain space, so a phrase spanning an ASS `\h` matched nothing. The
extractor is right to keep the hard space — `SubtitleFilterModel` now folds it on
both sides, and only when the pattern contains a space, so single-word searches
keep the optimised `QString::contains()` path.

## The render canary

The one thing the suites above cannot see is whether a picture appeared. That is
also the thing this environment lies about most, so it has its own tool:

```bash
./tools/render-canary.sh                 # testclip.mp4, the right clip for it
./tools/render-canary.sh some-film.mkv   # any clip, if it is bright and moving
```

It launches the player in a window of stated shape — its own `XDG_CONFIG_HOME`,
panel hidden, muted, so the developer's settings neither affect it nor get
written to — waits for `VO: [libmpv]`, grabs two frames 1.5 s apart through
`wsl-screenshot.ps1`, and answers three questions with numbers:

- **black** — the middle of the pane is above a luma floor, so an unpainted
  surface is not mistaken for a dark shot;
- **frozen** — the two grabs differ, which is the other face of the degraded
  state: one frame repeated while the transport clock advances normally;
- **content** — the grab's mean colour falls inside the range the clip's own
  centre region occupies, measured with ffmpeg over the file.

Both crops are the middle 40% of the frame, in the capture and in the clip
alike, so the comparison does not depend on where the window frame and the
transport bar put the picture, and chrome is nowhere near it. On a failure the
grabs and the log are copied to `canary-failure/`.

**All three checks have been made to fire**, which matters more here than the
passing case: a black clip trips black *and* frozen, and playing `testclip.mp4`
while comparing against a black reference (the optional second argument, which
exists for this) trips content. A healthy run reads:

```
canary: grabbed  luma 127.9 then 126.7, rgb (129,127,132)
canary: motion   8.07% of samples changed between grabs
canary: clip     rgb ranges r 118-131 g 122-136 b 119-133
canary: PASS  the picture is being painted, and it is this clip
```

It is a precondition, not a renderer test: it says whether visual evidence can
be trusted at all. When it fails, restart the distro before debugging the app.

Headless run of the app itself:

```bash
QT_QPA_PLATFORM=offscreen timeout 6 ./build/subixa testdata/subs.mkv
```

**Know what this does and does not check.** `offscreen` never renders the scene graph, so
`createFramebufferObject()` never runs, the mpv render context is never created, and the
queued `loadFile` is never flushed — mpv does not load the file at all (zero `[mpv]` log
lines). It verifies that the app starts and the QML parses, nothing about playback.
Subtitle parsing *does* run, because it never touches the render context, so this is a
real check on the extractor.

Anything about playback or rendering needs a real window. Check the log for
`VO: [libmpv]`; anything else means the render path is broken (see trap 1).

**Xvfb is not a usable substitute, and it fails misleadingly.** It was tried to test
llvmpipe with WSLg out of the picture (`Xvfb :99 -screen 0 2560x1440x24`, then
`QT_QPA_PLATFORM=xcb` and `import -window`). The app runs — 160% CPU, mpv logs
`VO: [libmpv]`, the clock advances internally — but the X window never repaints after
the first frame: three captures a second apart came back *byte identical*, transport
clock included. So the video pane looks black or half-drawn at every size, which reads
exactly like the rendering bug under investigation and is nothing of the kind. Any
pixel test that runs under Xvfb will report failures that are not there, which also
rules Xvfb out for the QQuickTest harness idea below as far as video pixels go.

## Seeing the UI from WSL

WSLg windows are Wayland surfaces and do **not** show up in an XWayland root grab
(`ffmpeg -f x11grab -i :0.0` comes back black, with or without `QT_QPA_PLATFORM=xcb`).
They *are* ordinary Win32 windows on the Windows side, hosted by `msrdc` and titled
`Subixa (<distro>)`, so drive the capture from there:

```bash
T=$(powershell.exe -NoProfile -Command '$env:TEMP' | tr -d '\r')
powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(wslpath -w tools/wsl-screenshot.ps1)" "$T\shot.png"
# then read /mnt/c/Users/<you>/AppData/Local/Temp/shot.png
```

`tools/wsl-input.ps1` clicks and types into the same window, so search, tabs and
click-to-seek can be exercised end to end without a human at the keyboard. Its
coordinates are window-relative and share an origin with the screenshot, so they can be
read straight off a capture. Note the panel layout shifts with content: with subtitle
tabs present the search box sits at y≈111, without them at y≈90.

**Both scripts must hold the foreground, and both used to assume they did.** Windows
refuses a focus change requested by a background process, so `SetForegroundWindow` from a
WSL-launched PowerShell often just loses — silently. The old screenshot script then
`CopyFromScreen`'d the window's *screen rectangle* and saved whichever window was on top,
which reads as a render bug in this app; the old input script clicked those coordinates,
sending clicks into an unrelated application. Now: the capture uses
`PrintWindow(PW_RENDERFULLCONTENT)`, which is z-order independent and steals no focus, and
falls back to a screen grab only after verifying foreground, printing `NOTFOREGROUND`
rather than saving the wrong pixels. `wsl-input.ps1` forces foreground, verifies it, and
aborts instead of clicking blind.

**`SW_RESTORE` un-maximises a maximised window.** Both scripts used to call
`ShowWindow(h, SW_RESTORE)` unconditionally before doing anything, so any attempt to check
a maximised or very wide window silently shrank it back to ~1186x733 first — destroying the
one piece of state a trap 10 / fullscreen test exists to exercise, and leaving
window-relative click coordinates pointing outside the window. Both now restore only when
`IsIconic()` says the window is actually minimised. A capture that comes back at the normal
window size after you maximised is this bug, not a failed maximise.

The foreground unlock is a synthetic ALT press, and **it must be released again after the
window is focused.** The keyup lands in the *old* foreground window, so the player can be
left thinking ALT is held, and then every `-Text` character arrives as an accelerator
(Alt+A, Alt+L, …) that a QML `TextField` ignores. The symptom is a click that demonstrably
worked — a tab switches — beside typing that vanishes with no error.

**Light text renders in the wrong colour on the D3D12 driver — and it is not the capture.**

This file used to say the opposite, and the correction is worth stating plainly, because
the old note sent anyone who saw it away from a real bug. Panel rows that are `#e8eaf0` on
`#12151c` come out **bright yellow**; `#aab2c2` comes out **bright green**; the 11 px
timestamp column at `#7e93b5` loses so much luminance it reads as **black and disappears
entirely**. Flat fills in the same window are exact to the byte, and dark-on-light text is
correct.

The old conclusion — chroma loss in the msrdc/RDP path — is wrong. Three things rule it out:

- A `Rectangle` filled with `#aab2c2` and a `Text` coloured `#aab2c2`, side by side in one
  window, render *differently*: the rectangle is right and the text is destroyed. No
  transport codec distinguishes a glyph from a quad.
- A vector `Shape` icon of the same stroke weight sitting directly above 24 px DemiBold
  text renders perfectly while the text does not.
- **The same build on llvmpipe is correct.** So is a stock `qml` runtime on llvmpipe.

And it reproduces with **no part of this project involved**: four `Text` items and one
`Rectangle` in a file run by Qt's own `qml` binary under `GALLIUM_DRIVER=d3d12` show it.
It is a Qt-on-Mesa-D3D12 bug in the glyph path — single-channel (R8/A8) texture sampling
is the likely mechanism, which is consistent with fills, shapes and video all being fine.

Neither `QQuickWindow::setTextRenderType(QtTextRendering)` nor
`QFont::NoSubpixelAntialias` fixes it; both are set anyway because both are right
independently. **The only known workaround is to avoid the driver: `SUBIXA_NO_GPU=1` renders
the UI correctly.**

That leaves a genuine trade, unresolved at the time of writing, and it should be settled
before release because it decides whether the product's central feature is legible:

| | UI text | video |
|---|---|---|
| D3D12 | destroyed | 10-bit native, any window size |
| llvmpipe | correct | capped to 1280x720 by `FboCap`, ~800% CPU on 720p |

The shape of the fix is to extend `GraphicsSetup`'s existing child-process probe — it
already re-runs the binary to check `GL_RENDERER` — so it also samples a single-channel
texture and rejects a driver that gets it wrong, with an override in Settings.

Consequence for screenshots meanwhile: **verify colours on rectangles, never on glyphs**,
and check which driver the run used before reading anything into text colour.

Two things to know before believing a black window:

- **The whole WSLg session can degrade into painting black, and stays that way.** Runs
  that log `VO: [libmpv] ... yuv420p` and advance the clock normally paint black at *every*
  window size, including files and timestamps that rendered correctly minutes earlier in
  the same session. It sets in after many launch/kill cycles — roughly seven was enough
  once, so budget the render-sensitive checks early rather than re-launching freely.

  It does **not** always present as pure black. It has also appeared as a *frozen partial
  frame*: `testclip.mp4` painting one wedge of a colour-bar frame, the rest black, byte
  identical at 0:01 and again at 0:14 while the transport clock advanced normally. Same
  class of failure, same fix. The tell is the freeze — a real decode/render bug would still
  change with the picture. Confirm against the source before believing any dark or partial
  frame: `ffmpeg -ss <t> -i <file> -frames:v 1 out.png`, plus
  `-vf signalstats,metadata=print:key=lavfi.signalstats.YAVG` for average luma (on 10-bit
  the scale is 0..1023, so ~307 is a normally lit shot, not a black one). What it is **not**: not the
  sync bug in trap 10 (it reproduces with `SUBIXA_NO_SYNC=1` and without, identically), not
  instance count (one process alone still fails), not memory (10 GB free), and weston.log
  shows nothing. Still unexplained.

  The reset is `wsl --terminate <your-distro>` **from Windows** — targeted, so it leaves
  any other distro alone. Do **not** use `wsl --shutdown`, which stops every distro you
  have running rather than just this one. Terminating also kills any shell or editor
  session running inside that distro, so save first.

  Practical consequence: **verify render fixes early in a session.** Once it degrades,
  every visual check returns a false negative.
- **Dark content is not a bug.** Feature films open on studio cards and near-black
  footage — seek a third of the way in before concluding anything about the render path.
- **Audio init sometimes times out** (`Init failed: Timeout`, then mpv walks pulse → alsa
  → jack → sdl). It follows a hard-killed previous instance, and playback will not start
  until it resolves. Kill instances with `SIGTERM`, not `-9`, and let one exit before
  starting the next.
- The app must be launched detached from the tool call that starts it, or it dies with
  the shell and the screenshot catches nothing.
- **`pkill -f subixa` kills the shell running it** — the pattern matches that
  shell's own command line, so the call dies with exit 144 before doing anything useful.
  Match on the argument instead (`pkill -f '[c]ustom_media_player /mnt'`).
