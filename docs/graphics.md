# Graphics under WSL

> How a GL driver gets chosen, and why the choice matters more here than it should.
> Split out of `CLAUDE.md`, which is the entry point and links here.

## Hardware GL under WSL: `GALLIUM_DRIVER=d3d12`

WSLg falls back to llvmpipe by default here (Qt's EGL path fails with `failed to create
dri2 screen` and lands on swrast), which is where every rendering trap below comes from.
That fallback is **not necessary on this machine**, and **the player now sorts it out by
itself** — no environment variable to remember:

```
graphics: hardware GL confirmed by probe: D3D12 (Intel(R) UHD Graphics 770)
graphics: hardware GL (remembered)
graphics: SUBIXA_NO_GPU set, leaving the driver alone
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
only the first launch pays for it. `SUBIXA_NO_GPU=1` opts out and stays on software, which is
how to test the workarounds.

Setting `GALLIUM_DRIVER` by hand still works and is honoured as-is:

```bash
GALLIUM_DRIVER=d3d12 ./build/subixa testclip.mp4
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
still prints the EGL dri2 warning — both are harmless here.

**`hwdec` is no longer hardcoded, and it changes nothing here.** Decode starts on the CPU
and `MpvEngine::applyHardwareDecoding()` asks for `hwdec=auto-safe` once `GL_RENDERER`
proves there is a real GPU — the same check that gates the software workarounds, so the two
decisions cannot disagree. Under WSL the answer is still software, and now for a stated
reason rather than by assumption: there is **no `/dev/dri`** at all, so there is no VA-API
render node to decode into, and mpv says so itself in the log:

```
hardware GL: asking mpv for hardware decoding (hwdec=auto-safe)
decoder: hwdec-current = no
```

`auto-safe` falls back silently rather than producing a black picture, so this costs
nothing, and on a native Linux desktop with a render node it will pick up vaapi/nvdec
without further work. `SUBIXA_HWDEC=<value>` pins it to anything mpv accepts (`no`, `auto`,
`vaapi`) and switches the automatic choice off.

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
`grim`/`import`. Nothing is hardcoded for WSL's sake any more — `hwdec` follows the same
software-rasterizer check as the workarounds, so a machine with a render node should pick
up vaapi/nvdec on its own. That is reasoned, not measured: WSL has no `/dev/dri` to try it
against, so **`hwdec-current` on a native Linux desktop is worth a look at port time.**
