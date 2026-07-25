# custom media player

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
- Click a row → seek there
- Auto-follow: the current line highlights and scrolls into view as playback advances

**Player** (enough to make the above usable)

- Open/drag-drop, transport controls, seek bar, volume, playback speed
- Audio/subtitle track selection
- Keyboard shortcuts, fullscreen
- Remembers position per file

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
`CMP_NO_GPU=1` forces the software path back for testing. The app logs both the choice it
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

### Milestone 4 — Polish

- Settings persistence
- Error surfaces for unsupported/corrupt files
- Theming for the browser panel
- Export a track to `.srt`

## Build

Requires Qt 6.9 (system Qt 6.4 on Ubuntu 24.04 is too old), libmpv, and FFmpeg dev
libraries.

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config \
  libmpv-dev libavformat-dev libavcodec-dev libavutil-dev libavfilter-dev libswscale-dev

export CMAKE_PREFIX_PATH="$HOME/Qt/6.9.3/gcc_64"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/custom_media_player /path/to/video.mkv
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

Running under WSLg means software decode *and* software rendering. That is expected and
fine for development; it is not a bug to chase. It does mean the renderer carries three
workarounds for Mesa's software rasterizers, all switching themselves off on a real GPU:

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

Three headless suites. `tst_subtitles` covers the extractor against the fixtures and the model
layer underneath the browser — the cue binary search, the search filter, and the row mapping
auto-follow depends on. `tst_mpvtracks` links libmpv with `vo=null` and checks that selecting
a track changes what mpv would render, comparing its `sub-text` property rather than looking
at pixels. `tst_playbackhistory` covers the resume-position store and its policy.

They avoid needing a window on purpose: under WSLg a screenshot is the *least* reliable
evidence available, since the session can degrade into painting stale frames while mpv and
the models keep working correctly.

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
