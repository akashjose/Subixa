# CLAUDE.md

Guidance for Claude Code working in this repo.

This file is the entry point and stays short on purpose. The detail lives in
`docs/`, linked from each section below — read the relevant one before changing
that area, particularly `docs/traps.md`.

## What this is

**Subixa**, a media player built from scratch: **Qt 6.9 + QML + libmpv**. Not a
fork of VLC, Haruna or SMPlayer — libmpv is a dependency, not a base.

The distinguishing feature is a **docked subtitle browser** (PotPlayer-style):
per-track tabs or a picker, timestamped rows, search, click-to-seek, auto-follow,
timing offset. Everything else is table stakes to support that. The audience is
people who *read* subtitles — language learners, subtitle editors, QC passes —
which is the tie-breaker whenever a decision is close.

It began as an MVP called *custom media player*; the rename to **Subixa** landed
in milestone 5 and now runs the whole way through — the repository directory, the
CMake project, the binary, the desktop entry and the `SUBIXA_*` environment
switches. Older notes and commit messages that say `custom_media_player` are
talking about this repository before that pass.

## Environment

Developed on **WSL2, Ubuntu 24.04**. Keep the repo on the native ext4 fs,
deliberately **not** under `/mnt/*` — those are 9p mounts and several times
slower for build workloads. Nothing below is WSL-specific except the graphics
notes and the tooling in `tools/`; an ordinary Linux desktop needs none of it.

| | |
|---|---|
| Qt | 6.9.3 at `~/Qt/6.9.3/gcc_64` (system Qt is 6.4.2 — too old, do not use) |
| libmpv | 0.37, client API 2.2.0, from apt |
| FFmpeg | 6.1.1 (`libavformat` 60.16.100, `libavcodec` 60.31.102) |
| Mesa | 26.1.5 from the kisak-mesa PPA |
| Compiler | gcc 13.3.0, C++17, CMake 3.28.3 + Ninja |
| Display | WSLg. Hardware GL via D3D12 is available and selected automatically. |

**Graphics are the single most surprising thing about this environment** — the
driver is chosen by a probe, and the one it picks renders text in the wrong
colour. See **`docs/graphics.md`**, and trap 22.

## Build and run

```bash
cd ~/code/subixa
export CMAKE_PREFIX_PATH="$HOME/Qt/6.9.3/gcc_64"     # required, or CMake finds Qt 6.4
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/subixa testclip.mp4
```

`testclip.mp4` is a generated 15 s testsrc2 clip with burned-in timecode —
useful because you can visually confirm the rendered frame matches the seek bar.
It has **no subtitle track**; generate fixtures when working on the browser.

```bash
./testdata/make-fixtures.sh          # embedded, sidecar, shifted-timeline cases
./testdata/make-fixtures.sh --big    # plus huge.mp4, a 200k-cue ASS sidecar
cd build && ctest --output-on-failure
```

Environment switches, all off by default: `SUBIXA_NO_GPU`, `SUBIXA_NO_SYNC`,
`SUBIXA_NO_FBO_CAP`, `SUBIXA_NO_SUBTITLE_CACHE`, `SUBIXA_HWDEC`. Each disables a
workaround so it can be A/B'd; `docs/graphics.md` and `docs/traps.md` say what
each is for.

**First thing in a new session:** run `./tools/render-canary.sh`. It says whether
the picture is being painted, whether it is moving, and whether it is the right
clip — the three things the WSLg degraded state breaks while everything in the
log looks normal. Until it passes, every visual check will lie, and the fix is to
restart the distro rather than to debug the app.

See **`docs/testing.md`** for the six suites, the canary, and the Windows-side
screenshot and input tooling — including why a screenshot here needs care.

## Architecture

```
src/MpvEngine.{h,cpp}         playback: owns mpv_handle, every property and command
src/MpvVideoItem.{h,cpp}      the video surface: owns only the render context
src/PaintedText.{h,cpp}       text via QPainter, for drivers that miscolour glyphs
src/ShortcutRegistry.{h,cpp}  every keyboard action, its default, any rebinding
src/Subtitle*.{h,cpp}         extraction, caching, models, styling, search
src/PlaybackHistory.{h,cpp}   per-file resume positions and remembered tracks
src/Playlist.{h,cpp}          what plays next: the folder as a queue
src/GraphicsSetup.{h,cpp}     picks a GL driver before Qt makes a context
qml/Main.qml                  the window: services, actions, layout, file lifecycle
qml/Theme.qml                 the design system, as a singleton
qml/ui/*.qml                  the control library, replacing the Basic style
```

Three things to know before changing anything here:

- **Playback and the video surface are separate objects, and the order they are
  created in `main()` is load-bearing.** `MpvEngine` must outlive every render
  context that draws from it.
- **The panel is one component in two homes**, destroyed and rebuilt on every
  detach, so its view state lives in the caller.
- **Every colour, size and duration comes from `Theme`.** Two sets of literals
  are deliberate exceptions and are labelled where they sit.

Full detail, including the design system and the reasoning behind the subtitle
row, is in **`docs/architecture.md`**.

## Traps

**`docs/traps.md` has all 23 with the evidence.** They are numbered as identities,
not an order. The index:

| # | | |
|---|---|---|
| 1 | `vo=libmpv` before `mpv_initialize` | or mpv spawns its own window |
| 2 | No render context until first paint | an early `loadFile` is silently dropped |
| 3 | `target_include_directories(... src)` | or qmltyperegistrar fails elsewhere |
| 4 | `QOpenGLFramebufferObject` is in QtOpenGL | not QtGui, in Qt 6 |
| 5 | Every text subtitle decoder emits ASS | text starts after the 8th comma |
| 6 | Rebase per track, not wholesale | or a shifted file flattens to 00:00:00 |
| 7 | ffmpeg does not decode entities | we do |
| 8 | Bitmap vs text is a codec property | not a name list |
| 9 | Software rasterizers get 10-bit wrong | and say nothing |
| 10 | mpv's render must *finish* before Qt samples | plus an FBO cap above ~2.9 MP |
| 11 | A delegate cannot take `required property string text` | it collides |
| 12 | Only the browser decodes entities | so libass and the panel disagree |
| 13 | A `TabBar` writes its resets back into your state | gate on `populated` |
| 14 | `{\p1}` is a shape, not text | and the cache version must bump with it |
| 15 | `font.families` does not exist in Qt 6.9 | only `font.family` |
| 16 | `AbstractButton.icon` is FINAL | controls here take `iconName` |
| 17 | A `default property alias` swallows the component's own children | |
| 18 | `Layout.fillWidth` on a *nested layout* does not stretch it | use a spacer |
| 19 | `prefs: prefs` binds to itself, silently | qualify with `root.` |
| 20 | Qt 6.9 `Menu` defaults to a *native* popup | there is none under WSLg |
| 21 | `ScrollBar` and `ScrollIndicator` are different types | |
| 22 | **Mesa's D3D12 driver miscolours every glyph** | see below |
| 23 | XML forbids `--` inside a comment | the SVG icon renders black, silently |

Trap 22 is the one that will confuse you first. On this machine Qt Quick's text
materials render in the wrong colour — `#aab2c2` as green, an 11px `#7e93b5`
timestamp as black and invisible — while rectangles, images and Shapes geometry
in the same frame are exact. It is not ours: it reproduces in Qt's own `qml`
binary, and on Mesa 26.1.5 as well as 25.2.8. `PaintedText` works around it and
`AppText` selects between the two at runtime. **Every piece of text in the app
must go through `AppText`**, never a bare `Text`.

Trap 23 is its small cousin, and cost a cycle for the same reason: a silent
failure that looks like something else.

## Conventions

- C++17, 4-space indent, Qt naming (`m_` members, camelCase methods).
- Keep mpv-specific types out of `MpvEngine.h` — it forward-declares `mpv_handle`
  and takes `void*` in `handleMpvEvent` so mpv headers stay in the .cpp.
- QML talks to mpv only through `Q_INVOKABLE`/properties on `MpvEngine`. Do not
  reach into libmpv from QML.
- **All text goes through `AppText`**, and every one sets `textFormat`
  explicitly. `Text.AutoText` promotes anything that looks like markup to *full*
  rich text, which supports `<img src>` and fetches it — and plenty of strings
  here come straight out of a container's metadata.
- New UI takes its sizes, colours and durations from `Theme`. A literal `12` or a
  hex colour in a component is the thing the token system exists to stop. The two
  deliberate exceptions are the subtitle *rendering* colours in `Main.qml` and
  `ColorSwatch`'s presets, which describe how subtitles are drawn over the film
  and must not follow the UI scheme.
- Subtitle parsing must **not** block the GUI thread.

## Where things are

| | |
|---|---|
| `docs/roadmap.md` | **where the project stands and what is next — start here** |
| `docs/architecture.md` | modules, the design system, the subtitle row, caching |
| `docs/traps.md` | all 23, with the evidence and what was ruled out |
| `docs/testing.md` | the suites, the render canary, WSL screenshot and input |
| `docs/graphics.md` | driver selection, why development stays on WSL, the Windows port |
| `docs/keyboard.md` | default bindings |
