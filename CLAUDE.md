# CLAUDE.md

Guidance for Claude Code working in this repo.

## What this is

A media player built from scratch: **Qt 6.9 + QML + libmpv**. Not a fork of VLC,
Haruna, or SMPlayer — libmpv is a dependency, not a base.

The distinguishing feature is a **docked subtitle browser** (PotPlayer-style): per-track
tabs, timestamped rows, search, click-to-seek, auto-follow during playback. Everything
else is table stakes to support that.

## Environment

Runs in **WSL2, Ubuntu 24.04** (vhdx at `F:\WSL\Ubuntu-24.04`). The repo lives on the
native ext4 fs, deliberately **not** under `/mnt/*` — those are 9p mounts and several
times slower for build workloads.

| | |
|---|---|
| Qt | 6.9.3 at `~/Qt/6.9.3/gcc_64` (system Qt is 6.4.2 — too old, do not use) |
| libmpv | 0.37, client API 2.2.0, from apt |
| FFmpeg | 6.1.1 (`libavformat` 60.16.100, `libavcodec` 60.31.102) |
| Compiler | gcc 13.3.0, C++17, CMake 3.28.3 + Ninja |
| Display | WSLg. Defaults to llvmpipe, but **hardware GL is available** — see below. |

### Hardware GL under WSL: `GALLIUM_DRIVER=d3d12`

WSLg falls back to llvmpipe by default here (Qt's EGL path fails with `failed to create
dri2 screen` and lands on swrast), which is where every rendering trap below comes from.
That fallback is **not necessary on this machine**, and **the player now sorts it out by
itself** — no environment variable to remember:

```
graphics: hardware GL confirmed by probe: D3D12 (Intel(R) UHD Graphics 770)
graphics: hardware GL (remembered)
graphics: CMP_NO_GPU set, leaving the driver alone
```

`GraphicsSetup::configure()` runs before `QGuiApplication`, because Mesa reads
`GALLIUM_DRIVER` when it loads the driver on the first context. It only acts under WSL —
native Linux picks iris/radeonsi correctly on its own and interfering could only make it
worse, and Windows has no Mesa in the picture. It checks `/dev/dxg`, the host
`libd3d12.so` and Mesa's `d3d12_dri.so` are all present, then **probes in a child
process**: the same binary re-run with `--gl-probe`, which creates an offscreen context,
prints `GL_RENDERER` and exits non-zero if it got a software rasterizer.

The child matters. A forced `GALLIUM_DRIVER` does not fall back — if the driver cannot
load, context creation simply fails — so the risky attempt happens in a throwaway process
rather than in the player. The answer is cached in `QSettings`, keyed by kernel version, so
only the first launch pays for it. `CMP_NO_GPU=1` opts out and stays on software, which is
how to test the workarounds.

Setting `GALLIUM_DRIVER` by hand still works and is honoured as-is:

```bash
GALLIUM_DRIVER=d3d12 ./build/custom_media_player testclip.mp4
```

That one variable is sufficient — `/dev/dxg`, Mesa's `d3d12_dri.so` and the host's
`/usr/lib/wsl/lib/libd3d12.so` are all already present, and `ld.wsl.conf` already has the
loader path. The app logs which driver it got at startup, so there is no guessing:

```
GL_RENDERER: llvmpipe (LLVM 20.1.2, 256 bits) | 4.5 (Compatibility Profile) Mesa 25.2.8
GL_RENDERER: D3D12 (Intel(R) UHD Graphics 770) | 4.1 (Compatibility Profile) Mesa 25.2.8
```

**On the D3D12 path every software-rasterizer trap below disappears**, verified rather than
assumed: a 2560 px pane renders cleanly where llvmpipe paints it black, and `yuv420p10`
renders natively and correctly with the 8-bit workaround switched off. The 3 GB 10-bit AV1
film plays correctly at 30 min with 65 subtitle tracks loaded.

So llvmpipe is a *fallback*, not the environment. Use `GALLIUM_DRIVER=d3d12` for anything
about picture quality, large windows or fullscreen; drop it deliberately when testing the
software path and its workarounds. Note the D3D12 driver reports GL 4.1 rather than 4.5 and
still prints the EGL dri2 warning — both are harmless here. `hwdec=no` is still hardcoded,
so decode remains on the CPU either way; revisiting that is now worth doing (see Next up).

## Build and run

```bash
cd ~/code/custom_media_player
export CMAKE_PREFIX_PATH="$HOME/Qt/6.9.3/gcc_64"     # required, or CMake finds Qt 6.4
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/custom_media_player testclip.mp4
```

`testclip.mp4` is a generated 15 s testsrc2 clip with burned-in timecode — useful because
you can visually confirm the rendered frame matches the seek bar. It has **no subtitle
track**; generate one when working on the browser.

Subtitle fixtures (`testclip.mp4` has no subtitle track):

```bash
./testdata/make-fixtures.sh          # embedded, sidecar, shifted-timeline cases
./testdata/make-fixtures.sh --big    # plus huge.mp4, a 200k-cue ASS sidecar
```

### Tests

```bash
cd build && ctest --output-on-failure     # or run ./build/tst_subtitles directly
```

Three headless suites, none needing a window, a GL context, or a compositor —
deliberately, because a screenshot is the least reliable evidence available here
(see the degraded-state note below). Run these before reaching for the UI.

- **`tst_subtitles`** — the extractor against the fixtures (the 8-comma ASS field
  layout, entity decoding, both halves of the rebase rule) plus
  `SubtitleLineModel::indexAt()`, the filter, and `rowAt()`/`startMsAt()` mapping.
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
- **`tst_playbackhistory`** — the resume-position store against a temporary ini file,
  and mostly its policy: too short, barely started, and near-the-end all mean *do
  not resume*, and finishing a file clears a position saved earlier. Also that two
  films with the same basename in different directories do not collide — paths are
  hashed because `QSettings` reads `/` as a group separator — and that the same file
  named relatively and absolutely resolves to one entry.

The harness found one real bug on its first run, since fixed: search did not fold
U+00A0 to a plain space, so a phrase spanning an ASS `\h` matched nothing. The
extractor is right to keep the hard space — `SubtitleFilterModel` now folds it on
both sides, and only when the pattern contains a space, so single-word searches
keep the optimised `QString::contains()` path.

Headless run of the app itself:

```bash
QT_QPA_PLATFORM=offscreen timeout 6 ./build/custom_media_player testdata/subs.mkv
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

### Seeing the UI from WSL

WSLg windows are Wayland surfaces and do **not** show up in an XWayland root grab
(`ffmpeg -f x11grab -i :0.0` comes back black, with or without `QT_QPA_PLATFORM=xcb`).
They *are* ordinary Win32 windows on the Windows side, hosted by `msrdc` and titled
`custom media player (<distro>)`, so drive the capture from there:

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
  sync bug in trap 10 (it reproduces with `CMP_NO_SYNC=1` and without, identically), not
  instance count (one process alone still fails), not memory (10 GB free), and weston.log
  shows nothing. Still unexplained.

  The reset is `wsl --terminate Ubuntu-24.04` **from Windows** — targeted, and it leaves
  the 22.04 distro's containers alone. Do **not** use `wsl --shutdown`, which stops every
  distro including the one hosting live Immich/Jellyfin containers. Note that terminating
  also kills any Claude Code session running inside that distro.

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
- **`pkill -f custom_media_player` kills the shell running it** — the pattern matches that
  shell's own command line, so the call dies with exit 144 before doing anything useful.
  Match on the argument instead (`pkill -f '[c]ustom_media_player /mnt'`).

## Architecture

```
src/MpvObject.{h,cpp}         QQuickFramebufferObject + libmpv OpenGL render API
src/main.cpp                  forces OpenGL RHI, passes argv[1] to QML as `initialFile`
src/GraphicsSetup.{h,cpp}     picks a GL driver before Qt makes a context, probing first
src/SubtitleTypes.h           SubtitleLine / SubtitleTrack plain structs
src/SubtitleExtractor.{h,cpp} libavformat/libavcodec parsing, runs on a worker thread
src/SubtitleManager.{h,cpp}   QML-facing owner of the worker and the parsed tracks
src/SubtitleLineModel.{h,cpp} QAbstractListModel over one track's cues
src/SubtitleFilterModel.{h,cpp} search proxy + the row mapping auto-follow needs
qml/Main.qml                  video + transport, and the window the panel lives in
qml/SubtitlePanel.qml         the browser itself, docked or in its own window
tools/wsl-*.ps1               screenshot and input injection from the Windows side
```

`MpvObject` owns an `mpv_handle`; the nested `MpvRenderer` (render thread) owns the
`mpv_render_context` and draws into the FBO. mpv state reaches QML through observed
properties (`time-pos`, `duration`, `pause`) surfaced as Qt properties.

`MpvRenderer` carries three workarounds for Mesa's software rasterizers, all keyed off
`usingSoftwareRasterizer()` (a `GL_RENDERER` string check) so a real GPU is untouched:
8-bit conversion for 10-bit video, a `glFinish()` before Qt samples the FBO, and the
framebuffer cap in `FboCap.h`. Traps 9 and 10 explain why each is needed and what was
ruled out first — do not remove any of them without reading those.

The cap is why `MpvObject` sets `setTextureFollowsItemSize(false)`. With it on, Qt
compares the FBO's size against the item's every frame and destroys any that disagrees,
so a capped framebuffer would be recreated forever; with it off, recreation is asked for
explicitly in `MpvRenderer::synchronize()` when the target size changes.

**Parsing cost is I/O, not cues.** One `av_read_frame` loop collects every subtitle
stream in a single pass, so 65 tracks cost the same walk as one — but subtitle packets
are interleaved through the container, so the whole file has to be read. Measured:

| file | tracks / cues | parse |
|---|---|---|
| the 3 GB AV1 film on a 9p mount | 65 / 93 350 | **9.4 s** |
| `huge.mp4` + 200k-cue ASS sidecar, local ext4 | 1 / 200 000 | **0.8 s** |
| `subs.mkv`, local | 3 / 11 | 0.06 s |

Twice the cues in a ninth of the time, so optimising the text parsing would buy nothing.
The extractor reports progress by bytes read for the same reason. Because the pass is
single, no track finishes early — publishing tracks one at a time as they complete would
not shorten the wait, which is why the panel shows a percentage instead.

`SubtitleManager` runs a `SubtitleExtractor` on its own `QThread` and republishes results
as bindable properties. Requests carry a monotonic id; bumping it makes an in-flight parse
abandon its demux loop, so opening a second file does not wait on the first.

Rows reach QML through one `SubtitleLineModel` per track. The model **shares** the track's
line buffer rather than copying it — `QVector` is implicitly shared and nothing mutates it
after `setLines()`, so handing 200k cues to a model is a refcount bump. That is what
replaced milestone 1's `QVariantList` snapshot, which cost ~600 ms of GUI thread per track
switch. `tracks` stays a `QVariantList`: it is per-track metadata only, rebuilt once a load.

Models are owned by the manager, **reused across loads and never deleted** — a QML binding
can still hold one for an instant after the track list changes, and an emptied model is
harmless where a dangling pointer is not. `model()` also pins ownership with
`QQmlEngine::setObjectOwnership(..., CppOwnership)`; without it the engine takes JavaScript
ownership of anything returned from an invokable and can collect it out from under C++.

`SubtitleFilterModel` is the search proxy. Filtering is a plain case-insensitive substring
match on the text role — timestamps are deliberately not searched, since someone typing
"12" means words. It also owns `rowAt(positionMs)`, which is what auto-follow calls: the
source model's binary search, mapped through the filter, returning -1 when the current cue
is filtered out (nothing to highlight, which is the wanted behaviour).

**Why the render API and not `--wid`:** with `--wid`, mpv draws into a separate native
surface *above* the scene graph, so QML cannot reliably paint over it. The whole feature
premise is QML chrome composited on the video, so `--wid` is not an option.

## Traps — all of these have already cost time

1. **`vo=libmpv` must be set before `mpv_initialize`.** Without it mpv picks a native VO
   (`wlshm` under WSLg), creates **its own Wayland surface**, and never touches the FBO.
   Video appears *outside* the app window and steals input. Looks like a compositing bug;
   it is not.
2. **The render context does not exist until the item first renders.** It is created in
   `createFramebufferObject()`, which runs *after* QML's `Component.onCompleted`. Calling
   `loadFile()` earlier gives `No render context set` → `Video: no video`, with no picture
   and no obvious error. Early loads are queued and flushed in `onRenderContextReady()`.
   **Any new mpv command that must run before first paint needs the same treatment.**
3. **`target_include_directories(... PRIVATE src)` is mandatory.** qmltyperegistrar emits
   `#if __has_include(<MpvObject.h>)`; without `src/` on the include path that guard is
   silently false and the build fails with `QQuickItem was not declared` — nowhere near
   the real cause.
4. **`QOpenGLFramebufferObject` is in QtOpenGL, not QtGui** (Qt 6 moved it;
   `QOpenGLContext` stayed in QtGui).

### Subtitle extraction

5. **Every ffmpeg text subtitle decoder emits ASS, and the field layout is not the one in
   an `.ass` file.** SRT, ASS and `mov_text` all come back as
   `ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text` — the text starts
   after the **8th** comma, and the first field is a read order counter, *not* a
   timestamp. Timings come from the packet, not the payload. Splitting as if it were a
   file's `Dialogue:` line silently eats the first words of every cue.
6. **Rebase against the container start time per track, not wholesale.** mpv shifts
   playback to start at zero (`--rebase-start-time`, on by default), so an MPEG-TS that
   starts an hour in needs the same shift or every seek lands 3600 s out. But muxers do
   produce files whose video starts at 1 h while the subtitle stream still starts at 0
   (`testdata/shifted.mp4`); subtracting there flattens every cue onto `00:00:00`. Only
   shift a track whose own first cue is at or past the offset.
7. **ffmpeg does not decode character entities.** Its SRT/WebVTT decoders convert `<i>`
   into ASS override tags but leave `&amp;`, `&#39;` and friends literal — they would show
   up raw in the browser *and* break search. Decoding is on our side.
8. **Bitmap vs text is a codec property, not a name list.** `avcodec_descriptor_get()`
   exposes `AV_CODEC_PROP_TEXT_SUB` / `AV_CODEC_PROP_BITMAP_SUB`; use those rather than
   matching codec names, and the classification stays right as codecs are added.
   (ffmpeg cannot transcode text to bitmap, so there is no way to *generate* a PGS/VOBSUB
   fixture locally — that path is verified against the codec table, not a file.)

### Rendering — both are software-rasterizer bugs, both fixed conditionally

9. **Mesa's software rasterizers render 10-bit video wrong, and say nothing about it.**
   A `yuv420p10` file comes out either fully black (synthetic 10-bit H.264) or heavily
   vertically striped (a real AV1 film), while a byte-for-byte 8-bit twin of the same clip
   renders perfectly in the same session. mpv logs no error at `-v`: it reports
   `Texture for plane 0/1/2` and `Using FBO format rgba16f` identically for both depths,
   so the log will not tell you. `MpvRenderer::usingSoftwareRasterizer()` checks
   `GL_RENDERER` for llvmpipe/softpipe/swrast/"Software Rasterizer" and, when it matches,
   applies `vf=format=yuv420p` — a no-op for 8-bit content, a CPU conversion for 10-bit.
   It is deliberately conditional so a real GPU keeps the native path.

   The workaround must be applied **before** the queued file starts playing, which is why
   it is queued ahead of `onRenderContextReady()` — see trap 2.

   Do not misread this as slow decode. AV1 1920x800 decodes at **25× realtime** here
   (20 cores, measured with `ffmpeg -f null -`); software *decode* is not the bottleneck,
   software *rendering* is.

10. **mpv's render must be *finished*, not just issued, before Qt samples the FBO.**
    Symptom: above roughly **2048 px of video-pane width** the picture goes black, or
    streaked, or shows a fine mesh of unwritten pixels. Below it, everything looks fine.
    A conformant driver tracks the render-to-texture dependency itself; llvmpipe does not,
    so the scene graph composites a partially rasterised surface.

    How that was established, because every cheaper explanation was wrong:

    - Not resize handling — a window that is 2400x1300 *from launch* fails identically.
    - Not a driver limit — `GL_MAX_TEXTURE_SIZE`, `GL_MAX_RENDERBUFFER_SIZE` and
      `GL_MAX_VIEWPORT_DIMS` all report **16384**. A 2060-wide FBO is legal.
    - No GL error is raised, at any point, draining the queue every frame.
    - Not two contexts: the renderer and the scene graph share one
      (`OpenGLContextResource` compares equal), so this is not the shared-context
      flush rule.
    - **Not mpv.** Dumping the FBO with `toImage()` at the failing size yields a
      *perfect* frame. The readback is itself the missing synchronisation, which is
      why the dump looks right while the screen does not.
    - Not sampling geometry — filling the FBO with four `glScissor`+`glClear`
      quadrants renders sharp, correctly placed quadrants at the failing size. (A
      uniform fill proves nothing here: it looks identical under any scaling error.)

    `glFinish()` after `mpv_render_context_render()` fixes it. `glFlush()` is **not**
    enough — it submits the work without waiting, which visibly improves the frame but
    leaves a fine grid of unwritten pixels. Gated on `usingSoftwareRasterizer()` so a
    real GPU is not stalled every frame for a bug it does not have. `CMP_NO_SYNC=1`
    disables the call, which is how to A/B it.

    **`glFinish()` is necessary but NOT sufficient — the fix is incomplete.** Adding
    fullscreen exposed this immediately, and it is not a fullscreen bug: it tracks FBO
    size, in a window as much as out of one. Measured on `testclip.mp4`, software
    rasterizer, sync on unless stated:

    | video pane | mode | result |
    |---|---|---|
    | 2216x1290 | windowed | clean |
    | 2220x1345 | fullscreen | fine mesh of unwritten pixels |
    | 2560x1345 | windowed | **fully black** |
    | 2560x1345 | fullscreen | fine mesh |
    | 2560x1345 | fullscreen, `CMP_NO_SYNC=1` | fully black |

    So `glFinish()` still buys a great deal — without it a large pane is black rather
    than meshed — but somewhere above roughly 2.9 megapixels of FBO it stops being
    enough, and the symptom becomes exactly what `glFlush()` alone used to produce.
    Note how close the clean and broken cases are (2216x1290 versus 2220x1345): this is
    a threshold in total FBO area, not a width cliff, so do not trust a single
    resolution to tell you the path is healthy.

    **Fixed by capping the FBO** (`FboCap.h`), which is what makes fullscreen usable on
    the software path: a 2560x1388 pane now renders into 1280x720 and Qt scales it, so
    the corrupt regime stops being reachable rather than being pushed slightly further
    out. Verified at the size that produced the mesh. The price is a softer picture when
    the window exceeds the video, which is the trade every player makes.

    Two terms, both needed. The *native* term stops a 720p file being rendered into a
    2560 px surface for no gain. The *area* term is what saves 4K, where the native size
    is above the pane and the native term would never engage at all. Both are gated on
    `usingSoftwareRasterizer()`: libass draws subtitles into this same FBO, so capping
    renders subtitle text at video resolution and upscales it — the last thing to blur in
    a player built around subtitles, and pointless on a GPU that has no need of it.
    `CMP_NO_FBO_CAP=1` disables the cap, the way `CMP_NO_SYNC=1` disables the glFinish.

    It also cuts CPU, though **not by as much as this file used to claim**. Measured on a
    3-minute 720p clip, fullscreen, steady state: **1435% CPU uncapped, 826% capped** —
    about 1.7x, not the ~6x guessed at before the cap existed. The remainder is Qt
    compositing a 2560x1440 scene through llvmpipe plus software decode, neither of which
    the cap touches.

    **Is this llvmpipe generally, or WSLg?** Unresolved, and worth knowing before spending
    much on the cap. It is *not* the Wayland surface specifically: switching to XWayland
    (`QT_QPA_PLATFORM=xcb`) keeps the corruption and only changes its severity — a fine
    mesh where the Wayland path paints solid black. But both still run through WSLg, so
    that does not exonerate it. The Xvfb attempt to remove WSLg entirely produced no
    evidence either way for the reason recorded in the Tests section: the window never
    repaints there, so every size looks broken. Settling it needs llvmpipe on a Linux
    desktop that is not WSLg — a VM with a compositor, or real hardware with
    `LIBGL_ALWAYS_SOFTWARE=1`.

    Until that lands, **fullscreen and very large windows show a corrupt picture on the
    software rasterizer** — so run with `GALLIUM_DRIVER=d3d12`, where the same 2560 px pane
    is clean. "A real GPU is unaffected" is no longer an assumption: it was measured on the
    D3D12 path, at the exact size that paints black under llvmpipe.

    That also lowers the urgency of the cap. It is still worth doing — it is the only fix
    for anyone stuck on software rendering, and it cuts CPU — but it is a fallback-path fix
    rather than a blocker, and it must stay gated on `usingSoftwareRasterizer()` for a
    reason beyond safety: libass draws subtitles into this same FBO, so capping it renders
    subtitle text at video resolution and upscales it. In a player whose whole point is
    subtitles, that is the last thing to blur on a GPU that has no need of the cap.

### Browser UI

11. **A delegate cannot take `required property string text`.** `ItemDelegate` already has
    a `text` property, and the role of the same name collides with it. Take
    `required property var model` and read `model.text` instead.
12. **Only the browser decodes entities, so mpv's own overlay disagrees with the panel.**
    libass renders `&amp;` literally over the video while the same cue reads `&` in the
    list. Both are behaving as designed (trap 7) — it is not a parsing regression.

Known-harmless: CMake warns `QTP0004` about qmldir files for `qml/`. Cosmetic.
mpv logs `Suspected software renderer`, EGL/DRM/Vulkan probe failures, and
`Cannot load libcuda.so.1` — all expected under WSL with `hwdec=no`.

## Conventions

- C++17, 4-space indent, Qt naming (`m_` members, camelCase methods).
- Keep mpv-specific types out of `MpvObject.h` — it forward-declares `mpv_handle` and
  takes `void*` in `handleMpvEvent` so mpv headers stay in the .cpp.
- QML talks to mpv only through `Q_INVOKABLE`/properties on `MpvObject`. Do not reach
  into libmpv from QML.
- Subtitle parsing must **not** block the GUI thread.

## Platform: why development stays on WSL

The build is Linux-only on purpose, and that is a decision rather than an accident.

WSL genuinely cannot test hwdec (software rendering only), GPU decode performance, or the
D3D11 RHI path. Those matter for shipping, but not for the subtitle browser, which is the
reason the project exists. Against that, a Windows build costs a second toolchain:
`CMakeLists.txt` discovers both libmpv and FFmpeg through `pkg_check_modules`, and that
path does not exist under MSVC — it would mean vcpkg or hand-wired `libmpv-2.dll` plus an
FFmpeg dev drop, a second Qt install, and a second build tree to keep green.

So the porting debt is deliberately kept small rather than paid early:

- All file handling goes through `QFileInfo`/`QDir`, with no POSIX-isms to unpick.
- The one known question is `SubtitleExtractor.cpp`'s `QFile::encodeName(path)` handed to
  `avformat_open_input`. ffmpeg wants UTF-8 on Windows and Qt's local 8-bit there is not
  necessarily UTF-8 — **verify against a non-ASCII filename at port time.** Correct as-is
  on Linux.

Revisit when Windows becomes a release target, or when hwdec, 4K/HEVC or HDR playback
needs judging — naturally after milestone 3.

**Native Linux** needs no porting: the 10-bit workaround in trap 9 disables itself on a
real GPU (`GL_RENDERER` stops matching), and the screenshot tooling is simply replaced by
`grim`/`import`. The one thing left hardcoded for WSL's sake is `hwdec=no` — on a machine
with a GPU that leaves vaapi/nvdec unused and burns CPU for nothing. Making it conditional
on the same software-rasterizer check is the obvious follow-up.

## Next up

Milestones 1 (extraction), 2 (browser UI) and 3 (player usability) are all committed and
the tree is clean. The panel and the picture agree in both directions, and the player has
fullscreen, keyboard shortcuts, a file dialog, drag-and-drop, volume, speed and per-file
resume. Verified against a real 3 GB AV1 film with **65 subtitle tracks / 93 350 cues**,
not just the fixtures — including resuming it at 29:44 after a kill.

**First thing in a new session:** play a fixture in a real window and confirm the picture
appears. The GPU is selected automatically now (see Environment); check the `graphics:`
line in the log to see which path it took. On the software path a
black or partial frame may be the degraded WSLg state rather than a real bug, and every
visual check will lie until the distro is restarted.

The renderer has no known correctness bugs left: the FBO cap (`FboCap.h`) closed the last
one, so fullscreen works on the software path as well as on D3D12.

Next, in this order:

1. **Milestone 4: polish.** Settings persistence beyond resume position, error surfaces for
   unsupported or corrupt files, theming for the panel, and exporting a track to `.srt`.
2. **A QML-level test harness**, if UI regressions start costing time — see the loose end.
3. **`hwdec=no`**, now that a GPU is reachable — see the loose end below.

Loose ends worth folding into whatever touches them next:

- **Parsed cues are not cached.** Reopening the film re-walks 3 GB for the same 93 350
  cues. Keying a cache the way `PlaybackHistory` hashes paths would make a rewatch
  instant; invalidation on mtime/size is the only fiddly part. This is the biggest
  remaining win on load time — progress reporting made the wait legible, not shorter.
- **The models now have a harness; the QML above them still does not.** `ctest` covers the
  extractor, `indexAt()`, the filter and `rowAt()` mapping, plus mpv's own track selection
  via `sub-text` — see the Tests section. What remains screenshot-only is the QML layer
  itself: that a tab click swaps the model, that follow scrolls the view, that the search
  box is wired to the proxy. A `QQuickTest`/`QQuickView` harness could reach those without
  a compositor and is the obvious next step if UI regressions start costing time.
- **No render canary yet.** Visual checks still have to be sanity-checked by hand against a
  known-good file, which is how the degraded state went unnoticed for a whole film test
  once. Playing `testclip.mp4` and diffing the grab against an `ffmpeg`-extracted reference
  frame programmatically would turn that into an explicit precondition. Note an in-process
  `grabWindow()`/FBO readback cannot serve as the canary — trap 10 records `toImage()`
  returning a *perfect* frame while the screen was wrong, because the readback is itself
  the missing synchronisation.
- **`hwdec=no` is hardcoded** in `MpvObject`'s constructor for WSL's sake. Conditional on
  the same `usingSoftwareRasterizer()` check, native Linux would get vaapi/nvdec for about
  five lines. See the platform section.
- **Search is a linear scan per keystroke**, coalesced by a 150 ms timer in QML. Fine at
  the 200k-cue fixture; if it ever is not, the fix is an index, not a longer timer.
- **The FBO cap's threshold is a guess, if a conservative one.** `FboCap::SafeArea` is 2.0
  MP, chosen below the observed boundary (2.86 MP clean, 2.99 MP corrupt) rather than at
  it, because trap 10 also records that a *trivial* draw at those sizes is fine — so the
  failure depends on render load as well as area, and a threshold sitting on the measured
  edge would not hold. Nobody has mapped whether the real variable is area, height, or
  something else; two panes of matched area and different shapes would answer it.
- **`hwdec=no` is worth revisiting now that a GPU is reachable.** It was hardcoded because
  WSL had no usable GPU path; `GALLIUM_DRIVER=d3d12` shows it does. Rendering is already on
  the GPU there while decode stays on the CPU, so the 3 GB AV1 film still burns cores for
  no reason. Whether Intel's WSL D3D12 exposes a usable vaapi/`d3d11va` decode path is
  untested.

## Related context

This project came out of a WSL 22.04 → 24.04 migration. Its checklist lives at
`/mnt/f/WSL-migration/CHECKLIST.md` and holds environment history, the Qt/aqt module
gotchas, and deferred restore work. The old `Ubuntu` (22.04) distro still exists and still
hosts live Immich/Jellyfin/filebrowser containers — **do not unregister it**.
