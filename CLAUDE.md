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

Subixa targets **three platforms**: native Linux, Windows (MSYS2 UCRT64, gcc
rather than MSVC) and WSL. All three are first class, and CI is expected to
produce output for each. **Check which one you are on before trusting anything
below** — much of the history in `docs/` was written under WSL and does not say
so, which is its own trap; see the note under Traps.

The current development host is **native Ubuntu 24.04 on X11**, not WSL.

| | |
|---|---|
| Qt | 6.12.0 at `~/data/Qt/6.12.0/gcc_64` (the distribution's is far too old) |
| libmpv | 0.41.0, client API 2.5.0, built from source |
| FFmpeg | 8.1.2 (`libavcodec` 62.28.102, `libavformat` 62.12.102) |
| libplacebo | 7.360.1, built from source |
| Mesa | 25.2.8, from the distribution |
| Compiler | gcc 14.2, **C++23**, CMake 3.28.3 + Ninja |
| Display | X11, `DISPLAY=:1`, hardware GL on Intel. `hwdec` resolves to `vaapi-copy`. |

**No distribution supplies that media stack** — Ubuntu 24.04 is on FFmpeg 6.1.1
with no upgrade path in apt — so `tools/build-deps.sh` builds libplacebo, FFmpeg
and mpv from pinned versions into `~/data/subixa-stack`. The versions move as a
deliberate step, because a player whose codec stack drifts is a player whose bug
reports cannot be reproduced.

## Build and run

```bash
export CMAKE_PREFIX_PATH="$HOME/data/Qt/6.12.0/gcc_64"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DSUBIXA_DEPS_PREFIX="$HOME/data/subixa-stack"
cmake --build build
./build/subixa testclip.mp4
```

`SUBIXA_DEPS_PREFIX` sets `PKG_CONFIG_PATH` and an rpath in one flag. The rpath
is the part that matters: without it a suite links against the prefix and then
loads the distribution's older libraries at run time, and `ctest` does not
inherit an `LD_LIBRARY_PATH` from your shell. Leave it unset to build against
system packages, which is what the Windows path does.

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

**`./tools/render-canary.sh` only runs under WSLg.** It says whether the picture
is being painted, whether it is moving, and whether it is the right clip — the
three things the WSLg degraded state breaks while everything in the log looks
normal. There, run it first in every session: until it passes, every visual check
will lie, and the fix is to restart the distro rather than to debug the app.

On the current host it exits immediately with *"cannot reach the Windows side"*,
because it shells to `powershell.exe`, `wslpath` and `tools/wsl-screenshot.ps1`.
Nothing has replaced it. On X11 the same three checks are a few lines — `import`
captures a window and two captures a second apart tell a moving picture from a
frozen one — and building that is open work, not something already available.

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
src/SettingsService.{h,cpp}   the settings file's owner: version, migration, pruning
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

**`docs/traps.md` has all 25 with the evidence.** They are numbered as identities,
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
| 24 | `mpv_create` fails outside a `C` locale | and says so only on a terminal |
| 25 | `<svg` must be in the first 1 KB | or gdk-pixbuf cannot see the icon at all |

**Every entry now carries a scope line saying which platform it applies to** —
reconstructed from the evidence after the lack of one had cost real time.
Development moved between Linux, Windows and WSL in stretches, so a trap was
found on whichever machine was in use and written up without saying so.
Several read as universal and are not: 20 and 22 are WSLg-only; 9 and 10 are
software-rasterizer-only; 24 and 25 were found on an ordinary Ubuntu desktop; the
non-ASCII path bug came off Windows. Trap 1 cuts the other way: found under
WSLg, but the ordering it demands is libmpv's contract and binds on every
platform. Trap 10's scope is the one still openly
unresolved — *"Is this llvmpipe generally, or WSLg?"*. Check the scope line
before treating a trap as a constraint, and keep it current in any entry you
touch.

Trap 22 is the clearest case. **It is a Mesa D3D12 bug, and that driver exists
only under WSLg** — on native Linux and on Windows it does not arise. There, Qt
Quick's text materials render in the wrong colour: `#aab2c2` as green, an 11px
`#7e93b5` timestamp as black and invisible, while rectangles, images and Shapes
geometry in the same frame are exact. It is not ours — it reproduces in Qt's own
`qml` binary. `PaintedText` works around it, and `AppText` chooses between the
two at runtime from the real `GL_RENDERER` rather than from the platform, so on
this host it resolves to the native path and costs nothing. **Every piece of text
in the app must still go through `AppText`**, never a bare `Text`, because the
WSL leg is a supported target.

Trap 23 is its small cousin, and cost a cycle for the same reason: a silent
failure that looks like something else.

## Conventions

- C++23 (no compiler extensions), 4-space indent, Qt naming (`m_` members,
  camelCase methods).
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
- **No commit hashes in comments or docs.** Name the change — "when the search
  proxy became hand-written" outlives a rebase or a history rewrite; a hash does
  not. Every hash in the tree went dangling at once when the branch was
  rewritten, and no sentence lost meaning when they were removed.

## Where things are

| | |
|---|---|
| `docs/roadmap.md` | **where the project stands and what is next — start here** |
| `docs/architecture.md` | the three founding decisions, modules, the design system, caching |
| `docs/traps.md` | all 25, with the evidence and what was ruled out |
| `docs/testing.md` | the suites, the conformance corpus, the render canary |
| `docs/building.md` | the full Linux build, including the from-source media stack |
| `docs/windows.md` | the MSYS2 toolchain, the build and the deployment sequence |
| `docs/graphics.md` | driver selection, and what the Windows port turned up |
| `docs/keyboard.md` | default bindings |
| `CHANGELOG.md` | what has been built, milestone by milestone |
| `CONTRIBUTING.md` | conventions, and what is most useful to work on |
| `README.md` | the public page: what this is, and how to build it |
