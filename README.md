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

Player controls are still minimal: playback comes from the file passed as `argv[1]`, and
there is no open dialog, track switching, volume, or fullscreen yet. The panel is also
independent of what mpv renders — selecting a tab changes what you *read*, not the
subtitles burned over the video. Wiring those together is the next task.

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

### Milestone 3 — Player usability (next)

- Audio/subtitle track switching wired to mpv — first, since it is what makes the panel
  and the picture agree
- Fullscreen + keyboard shortcuts
- File open dialog + drag-and-drop
- Volume, playback speed
- Resume position per file

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
fine for development; it is not a bug to chase. It does mean the renderer carries two
workarounds for Mesa's software rasterizers, both switching themselves off on a real GPU:

- 10-bit video (`yuv420p10`) renders black or striped, so it is converted to 8-bit first.
- mpv's rendering has to be explicitly finished before Qt samples the framebuffer, or
  anything wider than ~2048 px composites as a partially drawn frame.

`CLAUDE.md` traps 9 and 10 record how each was diagnosed and what was ruled out. Neither
should be removed without reading those; both look like arbitrary sledgehammers otherwise.

Two other WSL-specific gotchas live in `CLAUDE.md`: screenshots have to be taken from the
Windows side (`tools/wsl-screenshot.ps1`, with `tools/wsl-input.ps1` to drive the UI), and
the WSLg session can degrade into painting black at every window size, at which point
visual checks return false negatives until the distro is restarted.

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
