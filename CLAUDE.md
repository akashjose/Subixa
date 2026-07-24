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
| Display | WSLg. **Software rendering only** — no GPU decode. Expected, not a bug. |

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

Headless smoke test (no window, exits clean, good for CI-ish checks):

```bash
QT_QPA_PLATFORM=offscreen timeout 6 ./build/custom_media_player testclip.mp4
```

To verify rendering visually you need a screenshot — the window is a real WSLg window on
the Windows desktop. Check the log for `VO: [libmpv]`; anything else means the render
path is broken (see trap 1).

## Architecture

```
src/MpvObject.{h,cpp}   QQuickFramebufferObject + libmpv OpenGL render API
src/main.cpp            forces OpenGL RHI, passes argv[1] to QML as `initialFile`
qml/Main.qml            video + transport + docked subtitle panel (placeholder)
```

`MpvObject` owns an `mpv_handle`; the nested `MpvRenderer` (render thread) owns the
`mpv_render_context` and draws into the FBO. mpv state reaches QML through observed
properties (`time-pos`, `duration`, `pause`) surfaced as Qt properties.

**Why the render API and not `--wid`:** with `--wid`, mpv draws into a separate native
surface *above* the scene graph, so QML cannot reliably paint over it. The whole feature
premise is QML chrome composited on the video, so `--wid` is not an option.

## Traps — all four of these have already cost time

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

## Next up

See `README.md` → Roadmap. Immediate task is **Milestone 1: subtitle extraction** —
enumerate subtitle streams with libavformat and decode them to timestamped rows.
Subtitle text cannot come from mpv: it only exposes the currently displayed line, which
is useless for a browsable list. Parse the streams separately.

## Related context

This project came out of a WSL 22.04 → 24.04 migration. Its checklist lives at
`/mnt/f/WSL-migration/CHECKLIST.md` and holds environment history, the Qt/aqt module
gotchas, and deferred restore work. The old `Ubuntu` (22.04) distro still exists and still
hosts live Immich/Jellyfin/filebrowser containers — **do not unregister it**.
