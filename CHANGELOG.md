# Changelog

What has been built, milestone by milestone. Moved out of `README.md`, which is
now a page for someone deciding whether to use Subixa.

Nothing has been released, so there are no version tags to organise this by.
`docs/roadmap.md` is the forward-looking half.

## Unreleased — since milestone 5

**The settings file has one owner, and resume is a choice.** `SettingsService`
holds the schema version, migrates old files, and prunes per-file entries — a
year unwatched or past the newest 500 per group. The other writers borrow its
store instead of opening their own. On top of it, two independent toggles under
Settings → Playback: *Resume where you left off* and *Remember the subtitle
track per file* — separate because finishing a film clears its position and
must not forget the track. Off gates only the restore; the history keeps being
written, so switching back on remembers everything.

**CI, and the first packaging metadata.** `.github/workflows/ci.yml` runs three
jobs: freedesktop metadata validation, an Ubuntu 24.04 Debug/Release matrix that
builds the media stack from source — the prefix cached on a hash of
`tools/build-deps.sh`, because cold it costs twenty minutes — and runs every
headless suite, and an MSYS2 UCRT64 job that checks the rolling Qt against the
deliberate 6.12 floor and skips with a notice until MSYS2 ships it. The Windows
deployment sequence became a script, `tools/deploy-win.sh`, with a guard for
`windeployqt6`'s silent empty staging. And AppStream metadata exists and
installs: `com.akashjose.Subixa.metainfo.xml`.

**The dependency stack moved to current releases and is built from source.**
Linux was on Qt 6.9.3, FFmpeg 6.1.1 and mpv 0.37 while the Windows side was
already on FFmpeg 8 and Qt 6.11 — two platforms decoding subtitles with decoders
two majors apart. Ubuntu 24.04 cannot close that gap, so `tools/build-deps.sh`
builds libplacebo 7.360.1, FFmpeg 8.1.2 and mpv 0.41.0 from pinned versions into
a prefix. Qt is 6.12, the standard is C++23. Note that mpv 0.41 makes
libplacebo's `gpu-next` the default renderer, which changed the render path.

**The test suite stopped reporting green on a checkout it had not tested.**
Three suites called `QSKIP` when fixtures were missing, and `QSKIP` exits 0.
`REQUIRED_FILES` now fails them instead. A first attempt using
`SKIP_REGULAR_EXPRESSION` made it worse — it matches a test's whole output rather
than its exit status, so a run that printed the phrase *and* failed an assertion
*and* exited 1 was reported `***Skipped` with ctest exiting 0.

**A conformance corpus, committed as bytes.** `testdata/conformance/` is 35 KB
of containers built from a 32×32 token video stream, plus `golden.tsv` pinning
what the extractor makes of them cue by cue. Every container in the tree was
previously muxed at test time by whatever ffmpeg was installed, so two platforms
agreeing proved nothing.

**An application id**, `com.akashjose.Subixa`, keying the desktop entry and icon.
`StartupWMClass` stays `subixa`: X11 takes `WM_CLASS` from `applicationName`
while only Wayland uses the desktop file name.

**A Windows build**, under MSYS2 UCRT64 with gcc. See `docs/windows.md`.

**It runs on an ordinary Linux desktop.** Three things broke that WSL had
hidden: `mpv_create` returns null under any `LC_NUMERIC` but `C`; gdk-pixbuf
sniffs only a file's first kilobyte for `<svg`, so the icon was blank wherever
GNOME drew one; and nothing tied a running window to the desktop entry without
`setDesktopFileName`.

**The window answers the mouse.** Folder drops expanded depth-bounded with files
before subfolders, Esc minimises outside fullscreen, the wheel over the picture
is volume with deltas accumulated so a trackpad is not forty steps, and
right-click opens the same overflow menu the transport button does. That last one
exposed a defect the shared components had all along: a `Menu` never consults its
rows for its width.

## Milestone 5 — the pre-release pass

Driven by two reviews, one architectural and one of the interface.

- Playback split out of the scene-graph item into `MpvEngine`, so teardown runs
  in the order libmpv requires — the previous order was a use-after-free no
  headless test could reach
- Exact, millisecond-formatted seeks. `QString::number(double)` gives six
  significant digits, so clicking a cue three hours into a film landed 123 ms off
- A design system with measured contrast, a control library replacing the Basic
  style, vector icons, a two-row transport, a rebuilt subtitle row, and a
  settings window that did not previously exist
- Subtitle delay applied to the browser's timestamps as well as the picture, plus
  sync-to-this-line, per-cue seek, A-B loop, cue copy, screenshots, audio delay,
  picture adjustments, an OSD, a queue popover and remappable keys
- Release plumbing: GPL-3.0-or-later, install rules, a desktop entry, an icon,
  and the rename from the MVP's *custom media player* to **Subixa**

## Styling in the browser

- ASS override tags become markup: italics, bold, underline, speaker colours
- Cue colours adjusted to stay legible against the panel, hue preserved, at
  WCAG's 4.5:1. The hue is the part that says *which speaker*, so it survives
  while the lightness moves far enough to be read
- Vector drawings dropped from the list — `{\p1}m 0 0 l 100 0` is a shape, not
  dialogue
- Search unaffected: it matches the plain text, so what is shown and what is
  found cannot disagree, and styling can be turned off

## What plays next

- The folder is the queue: opening a file queues its siblings in natural order,
  so `ep2` comes before `ep10`, which plain alphabetical order gets backwards
- A drop of several files becomes the queue instead, in the order dropped
- Auto-advance at the end of a file, `<` and `>` to move by hand, and a `2/3`
  readout so an advance does not look like the player wandering off
- The current file is in the window title

There is no playlist panel, and that is deliberate: the subtitle browser is what
this player is for, and a second list competing with it would be the wrong thing
to build.

## Milestone 4 — polish

- Parsed cues cached on disk, keyed by path and invalidated on the size and mtime
  of the video **and every sidecar next to it**. A reopen of the 3 GB film costs
  ~110 ms instead of 15.7 s
- Settings persistence: window and panel geometry, docked or detached, volume,
  theme and row text size, plus the subtitle track remembered per file with the
  last-used language as the fallback for a new one
- Error surfaces — mpv's own end-of-file errors and extractor failures shown over
  the picture rather than only in the log
- Theming as a QML singleton, so both windows follow one switch
- Export a track to `.srt`, round-tripped through the parser in the tests
- `hwdec` chosen by the same probe that picks the driver, rather than hardcoded
  off. Under WSL the answer is still software decode, for a stated reason: there
  is no `/dev/dri` render node, and mpv reports `hwdec-current = no` when asked

## Milestone 3 — player usability

- Audio and subtitle track switching wired to mpv, in both directions. Tracks are
  matched by ffmpeg stream index through mpv's `ff-index`, since neither side's
  numbering follows from the other and language strings collide — the test film
  carries two English tracks
- Fullscreen and keyboard shortcuts, file open dialog and drag-and-drop, volume
  and playback speed
- Resume position per file, with a deliberately conservative policy: a clip under
  two minutes, the first thirty seconds and the last minute are all left
  unremembered, and finishing a film clears the position rather than dropping you
  back into the credits next time

This milestone also capped the video framebuffer on the software rasterizer,
which is what makes fullscreen usable there. A 2560 px pane renders into
1280×720 and comes out correct, for about 1.7× less CPU.

## Milestone 2 — browser UI

- A `QAbstractListModel` of subtitle lines, one model per track, tabs across
  tracks
- `QSortFilterProxyModel` for incremental search
- Click-to-seek
- Auto-follow with binary search on the current timestamp, plus a toggle so
  manual scrolling does not fight playback. Dragging the list turns following off
  rather than fighting you for the viewport, and the current line is kept in a
  band in the upper middle so the next few lines are always in view

The model shares each track's line buffer instead of copying it, so switching
tabs is a refcount bump — the 200k-cue fixture browses and follows without the
~600 ms GUI stall the milestone 1 snapshot cost.

## Milestone 1 — subtitle extraction

- Enumerate `AVMEDIA_TYPE_SUBTITLE` streams with libavformat; expose the track
  list and language metadata
- Decode packets to `AVSubtitle`, converting to `{startMs, endMs, text}` rows
- Handle the text formats: SRT, ASS/SSA, `mov_text`. Strip ASS override tags for
  display while keeping the raw text around
- Detect bitmap subtitles (PGS, VOBSUB) and mark them unsupported — they carry no
  text, so a browser would need OCR. Out of scope
- Load sidecar files next to the video alongside embedded tracks
- Run parsing off the GUI thread; large ASS tracks are slow enough to stutter the
  UI otherwise

Measured on the fixtures: 200,000 cues (27 MB of ASS) parse in ~1.2 s on the
worker thread, with playback ticking normally throughout.
