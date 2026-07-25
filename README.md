# Subixa

A desktop media player built on **Qt 6.9 + QML + libmpv**, with a first-class
**subtitle browser** as its reason for existing.

## Why

Most players treat subtitles as something you *watch*, not something you *read or
search*. PotPlayer is the exception, and its docked subtitle list is the feature worth
rebuilding: every line of every subtitle track, timestamped, searchable, and clickable to
jump straight to that moment.

That is the goal. Playback is the necessary substrate; the subtitle browser is the point.

## Target feature set

**Subtitle browser** (the core)

- One tab per subtitle track in the file
- Every line as a timestamped row
- Full-text search across the track
- The subtitler's own italics, bold and speaker colours, rendered rather than stripped
- Click a row → seek there
- Auto-follow: the current line highlights and scrolls into view as playback advances

- Search-hit navigation, so a term can be walked through a 200 000-cue track
- Subtitle timing offset, applied to the browser's own timestamps as well as to the
  picture — including "sync to this line", which computes the offset from a line you pick
  and the moment it should actually be spoken
- Loop a single line, copy it with or without its timestamp, export the track as `.srt`
- Optional reading-speed readout, flagged when a line runs faster than 21 characters a
  second

**Player** (enough to make the above usable)

- Open/drag-drop, transport controls, a seek bar that previews the line under the cursor,
  volume, playback speed with pitch correction
- Audio/subtitle track selection, chapters, A-B loop, screenshots, audio delay
- Subtitle appearance — font, size, colour, outline, shadow, position — configurable while
  playing, never in a config file
- Remappable keyboard shortcuts, fullscreen
- Remembers position, subtitle track, window and panel layout per file and per session
- Plays on to the next file in the folder, in the order a person would put them in

The browser docks beside the video or detaches into its own window (Ctrl+D), and is
resizable either way. Everything is reachable by mouse as well as by key: the transport's
overflow menu holds whatever the width has dropped, and **Settings** (Ctrl+,) covers
playback, subtitle appearance and timing, the browser, hotkeys and the interface.
`CLAUDE.md` lists the default shortcuts.

## Design decisions

**Built from scratch, not forked.** VLC, Haruna, and SMPlayer all carry architecture
shaped around their own UI goals. libmpv is a dependency here, not a base.

**mpv render API, not `--wid`.** The video surface is a `QQuickFramebufferObject`: mpv
renders into an FBO the app owns, so it is an ordinary scene-graph node and QML composites
freely on top. `--wid` would put video in a separate native surface *above* the scene
graph, which makes overlays and docked panels unreliable — fatal for this design.

**Subtitle text is parsed independently of mpv.** mpv only exposes the *currently
displayed* subtitle line, which cannot produce a browsable list. Subtitle streams are
demuxed and decoded separately with libavformat/libavcodec.

## Status

Video renders inside the window via the render API, QML composites over it, transport
controls track playback.

The subtitle browser works end to end: embedded and sidecar tracks are demuxed and decoded
to timestamped rows, then listed in the docked panel under one tab per track. Typing in the
search box filters the track as you type, clicking a row seeks to it, and the line playing
now is highlighted and scrolled into view — until you drag the list, which turns following
off rather than fighting you for the viewport.

Exercised on a real 3 GB AV1 film carrying **65 subtitle tracks and 93 350 cues**, with a
matching sidecar `.srt` picked up as a 66th: tabs per language, search across the track,
and follow landing on the correct cue after arbitrary seeks.

The browser can be resized by dragging its edge, or detached into its own window with
Ctrl+D — useful with the film fullscreen on another screen. The line playing now is kept
in a band in the upper middle of the list rather than at the bottom edge, so the next few
lines are always in view. Parsing a feature-length container takes a few seconds (it has
to be walked end to end whatever the cue count), and the panel reports how far along it is.

The panel and the picture now agree: selecting a tab tells mpv to render that track, and
choosing a track from the transport menu moves the panel to match. Files arrive by
`argv[1]`, a file dialog, or drag-and-drop, with keyboard shortcuts, fullscreen, volume and
playback speed alongside.

Reopening a film is now instant. The parse is all I/O — subtitle packets are interleaved
through the container, so the whole file has to be read whatever the cue count — and
nothing about the result changes between opens, so the cues are cached on disk against the
size and mtime of the video *and every sidecar beside it*. The 3 GB film went from **15.7 s
to about 110 ms**, and the panel says `cached` so a hit is not mistaken for a suspiciously
quick parse.

The list shows what the subtitler wrote, not a flattened version of it: a line spoken in
italics is italic in the browser, and a second speaker's colour is their colour. Cue colours
are chosen to sit over a picture, so they are adjusted where they would be illegible against
the panel — the hue survives, since that is the part that says *which speaker*, while the
lightness moves far enough to be read. Vector drawings, karaoke timing and positioning tags
are dropped: those say where to paint something over the video, and a list of lines has no
use for them. Search always matches the plain text, so what is shown and what is found
cannot disagree, and the whole thing can be switched off in the panel's More menu.

When a file finishes, the next one starts. Opening anything makes its folder the queue, so
an episode is followed by the next episode without anyone building a playlist — `ep2` before
`ep10`, which plain alphabetical order gets backwards. Dropping several files at once makes
exactly those the queue, in the order they were dropped. There is no playlist panel, and
that is deliberate: the subtitle browser is what this player is for, and a second list
competing with it would be the wrong thing to build.

The player also remembers what it should: window and panel geometry, docked or detached,
volume, theme and text size — and, per film, the resume position and **which subtitle track
was being read**. On a file with 65 tracks that last one is the difference between opening
where you left off and hunting for the right tab every time; a film with no history of its
own falls back to the language chosen last. A track can be exported to `.srt`, the panel
has a light and a dark scheme, and a file that will not play now says so over the picture
instead of leaving it black.

Software rendering used to fall apart at large window sizes — on Mesa's llvmpipe a video
pane above roughly 2.9 megapixels rendered black or with a fine mesh of unwritten pixels,
which made fullscreen unusable. The framebuffer is now capped there, to the video's own
size and to a safe area, with Qt scaling the result: a 2560 px pane renders into 1280x720
and comes out correct, for about 1.7x less CPU and a softer picture. The cap is off on any
real GPU, where it would only blur subtitles for no reason.

Under WSL the software path is avoidable entirely, and the player arranges that itself:
before creating a context it checks whether D3D12 passthrough is available, verifies it by
probing in a throwaway child process, and only then switches to hardware GL — where a
2560 px pane is clean at full resolution and 10-bit video renders natively with no
workarounds engaged. The result is cached, so only the first launch pays for the probe, and
`SUBIXA_NO_GPU=1` forces the software path back for testing. The app logs both the choice it
made and its `GL_RENDERER`, so which path is in use is never a guess.

## Roadmap

### Milestone 1 — Subtitle extraction ✅

- Enumerate `AVMEDIA_TYPE_SUBTITLE` streams with libavformat; expose track list + language
  metadata
- Decode packets to `AVSubtitle`, converting to `{startMs, endMs, text}` rows
- Handle the text formats: SRT, ASS/SSA, `mov_text`. Strip ASS override tags for display
  while keeping raw text around
- Detect bitmap subtitles (PGS, VOBSUB) and mark them unsupported — they carry no text,
  so a browser would need OCR. Out of scope for now
- Load sidecar files (`.srt`/`.ass` next to the video) alongside embedded tracks
- Run parsing off the GUI thread; large ASS tracks are slow enough to stutter the UI

Measured on the fixtures: 200 000 cues (27 MB ASS) parse in ~1.2 s on the worker thread,
with playback ticking normally throughout.

### Milestone 2 — Browser UI ✅

- `QAbstractListModel` of subtitle lines, one model per track, tabs across tracks
- `QSortFilterProxyModel` for incremental search
- Click-to-seek
- Auto-follow with binary search on the current timestamp, plus a toggle so manual
  scrolling does not fight playback

The model shares each track's line buffer instead of copying it, so switching tabs is a
refcount bump — the 200k-cue fixture browses and follows without the ~600 ms GUI stall the
milestone 1 snapshot cost.

### Milestone 3 — Player usability ✅

- ✅ Audio/subtitle track switching wired to mpv — the panel and the picture agree, in both
  directions. Tracks are matched by ffmpeg stream index through mpv's `ff-index`, since
  neither side's numbering follows from the other and language strings collide (the test
  film carries two English tracks)
- ✅ Fullscreen + keyboard shortcuts
- ✅ File open dialog + drag-and-drop
- ✅ Volume, playback speed
- ✅ Resume position per file — kept per file with a deliberately conservative policy: a
  clip under two minutes, the first thirty seconds, and the last minute are all left
  unremembered, and finishing a film clears the position rather than dropping you back
  into the credits next time

Milestone 3 also closed the renderer's last correctness bug: the video framebuffer is now
capped on the software rasterizer, which is what makes fullscreen usable there.

### Milestone 4 — Polish ✅

- ✅ Parsed cues cached on disk, keyed by path and invalidated on the size and mtime of the
  video and every sidecar next to it — a reopen of the 3 GB film costs ~110 ms instead of
  15.7 s
- ✅ Settings persistence: window and panel geometry, docked or detached, volume, theme and
  row text size, plus the subtitle track remembered per file with the last-used language as
  the fallback for a new one
- ✅ Error surfaces — mpv's own end-of-file errors and extractor failures are shown over the
  picture rather than only in the log
- ✅ Theming for the browser panel, as a QML singleton so both windows follow one switch
- ✅ Export a track to `.srt`, round-tripped through the parser in the tests
- ✅ `hwdec` chosen by the same probe that picks the driver, rather than hardcoded off

Under WSL the answer to that last one is still software decode, and now for a stated
reason: there is no `/dev/dri` render node, and mpv reports `hwdec-current = no` when asked.

### Milestone 6 — Styling in the browser ✅

- ✅ ASS override tags become markup: italics, bold, underline, speaker colours
- ✅ Cue colours adjusted to stay legible against the panel, hue preserved, at WCAG's 4.5:1
- ✅ Vector drawings dropped from the list — `{\p1}m 0 0 l 100 0` is a shape, not dialogue
- ✅ Search unaffected: it matches the plain text, and styling can be turned off

### Milestone 5 — What plays next ✅

- ✅ The folder is the queue: opening a file queues its siblings in natural order, so `ep2`
  comes before `ep10`
- ✅ A drop of several files becomes the queue instead, in the order dropped
- ✅ Auto-advance at the end of a file, `<` and `>` to move by hand, and a `2/3` readout in
  the transport so an advance does not look like the player wandering off
- ✅ The current file is in the window title

## Build

Requires Qt 6.9 (system Qt 6.4 on Ubuntu 24.04 is too old), libmpv, and FFmpeg dev
libraries.

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config \
  libmpv-dev libavformat-dev libavcodec-dev libavutil-dev libavfilter-dev libswscale-dev

export CMAKE_PREFIX_PATH="$HOME/Qt/6.9.3/gcc_64"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/subixa /path/to/video.mkv
```

Qt itself is installed via [aqtinstall](https://github.com/miurahr/aqtinstall):

```bash
pipx install aqtinstall
aqt install-qt linux desktop 6.9.3 linux_gcc_64 \
  -m qtshadertools qtimageformats qtmultimedia qt5compat -O ~/Qt
```

Note `qtdeclarative` is **not** a valid module for Qt 6 — QML and Quick ship in the base
package, and passing a bad module name aborts the whole install.

## Development notes

Running under WSLg means software decode — there is no `/dev/dri` render node to hand it
to — though rendering does reach the GPU through D3D12 passthrough. On the software
fallback the renderer carries three workarounds for Mesa's software rasterizers, all
switching themselves off on a real GPU:

- 10-bit video (`yuv420p10`) renders black or striped, so it is converted to 8-bit first.
- mpv's rendering has to be explicitly finished before Qt samples the framebuffer, or
  anything wider than ~2048 px composites as a partially drawn frame.
- The framebuffer is capped, because even with that the picture falls apart above roughly
  2.9 megapixels — and rendering a 720p file into a 2560 px surface costs CPU for nothing.

`CLAUDE.md` traps 9 and 10 record how each was diagnosed and what was ruled out. Neither
should be removed without reading those; both look like arbitrary sledgehammers otherwise.

Two other WSL-specific gotchas live in `CLAUDE.md`: screenshots have to be taken from the
Windows side (`tools/wsl-screenshot.ps1`, with `tools/wsl-input.ps1` to drive the UI), and
the WSLg session can degrade into painting black at every window size, at which point
visual checks return false negatives until the distro is restarted.

### Tests

```bash
cd build && ctest --output-on-failure
```

Five headless suites. `tst_subtitles` covers the extractor against the fixtures and the model
layer underneath the browser — the cue binary search, the search filter, and the row mapping
auto-follow depends on — plus the cue cache (a hit has to reproduce a parse exactly, and a
changed file or a new sidecar has to miss) and the `.srt` export, checked by parsing back
what it wrote. `tst_mpvtracks` links libmpv with `vo=null` and checks that selecting
a track changes what mpv would render, comparing its `sub-text` property rather than looking
at pixels. `tst_playbackhistory` covers the per-file store and its policy, and `tst_playlist` what
plays next — mostly the ordering and the boundaries, since a queue that wraps round to the
first file is how you watch episode one twice. `tst_qmlpanel`
loads the real `Main.qml` offscreen and drives the QML layer itself: tab clicks swapping the
model, search reaching the proxy, follow scrolling the view, a remembered track restored on
reopen, and view state surviving a detach.

They avoid needing a rendered frame on purpose: under WSLg a screenshot is the *least*
reliable evidence available, since the session can degrade into painting stale frames while
mpv and the models keep working correctly. The QML suite found two real bugs on its first
run — detaching the panel silently reset the reader's tab, and a handler that does not
exist on `FileDialog` — neither of which a compiler would have caught.

The picture itself is the one thing they cannot see, so it has a separate check:

```bash
./tools/render-canary.sh
```

It plays `testclip.mp4` in a window of its own choosing, grabs two frames a moment apart,
and reports whether the pane is painted at all, whether it changed between grabs, and
whether its colours match the clip's — the three ways the degraded session fails while
every log line looks healthy. All three checks have been made to fire deliberately, which
is the only reason to believe the passing case.

`testclip.mp4` is a generated 15-second clip with a burned-in timecode, so a screenshot is
enough to confirm the rendered frame matches the reported playback position. It carries no
subtitles; `testdata/make-fixtures.sh` builds files that do — embedded SRT/ASS/`mov_text`
tracks, sidecar files, and containers with shifted timelines. Only the small `.srt`/`.ass`
sources are in git; run the script to rebuild the rest.

```bash
./testdata/make-fixtures.sh          # normal fixtures
./testdata/make-fixtures.sh --big    # plus a 200k-cue stress file
```

See `CLAUDE.md` for build gotchas that have already cost time — particularly `vo=libmpv`
and render-context ordering.
