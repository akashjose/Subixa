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

## Platform: the development host, and the Windows build

Development stays on WSL, and that is a decision rather than an accident. WSL genuinely
cannot test hwdec (software rendering only), GPU decode performance, or the D3D11 RHI
path. Those matter for shipping, but not for the subtitle browser, which is the reason the
project exists — so the browser is developed where it is cheapest to develop, and the
things WSL cannot judge are judged elsewhere.

**A Windows build now exists**, and it cost less than this section used to estimate. The
estimate assumed MSVC, where the objection was real: `CMakeLists.txt` discovered libmpv and
FFmpeg through `pkg_check_modules`, there is no MSVC libmpv to point that at, and the way
out would have been vcpkg or a hand-generated import library. MSYS2 UCRT64 sidesteps all of
it by packaging libmpv, FFmpeg and Qt 6 against one another for gcc. What the port actually
took was a `WIN32` branch in the dependency lookup — `find_path`/`find_library` instead of
pkg-config, because MSYS2's `.pc` files hardcode `prefix=/ucrt64` and hand a native CMake
an MSYS path with no drive letter — and a handful of small fixes elsewhere. `windows.md` has
the toolchain, the versions and the deployment steps.

The porting debt was kept small rather than paid early, and mostly that held up:

- All file handling goes through `QFileInfo`/`QDir`, with no POSIX-isms to unpick. This was
  correct — nothing here needed touching.
- `glGetString` was being called bare in `MpvVideoItem.cpp`. On Linux that linked only
  because libGL arrived transitively; on Windows the symbol lives in `opengl32.dll` and Qt
  links no import library for it, since it resolves GL dynamically. It now goes through
  Qt's resolved function table, which is portable and was the better call anyway.
- `.srt` export opened its `QSaveFile` with `QIODevice::Text`, which is a no-op on Linux and
  silently turns every `\n` into `\r\n` on Windows — the same track exported on two machines
  would have come out byte-different. The flag is gone; every SubRip reader accepts LF.
- The command line is safe. Windows hands `main()` an `argv` in the ANSI codepage, which
  mangles a non-ASCII path before the program sees it, but Qt repopulates
  `QCoreApplication::arguments()` from `GetCommandLineW` and `QCommandLineParser` reads
  that. A path typed or dropped onto the binary arrives intact.

- **The one known question was the real one, the answer was that it was broken, and it
  is now fixed.** `SubtitleExtractor.cpp` handed `QFile::encodeName(path)` to
  `avformat_open_input`. Measured on this machine against a file named
  `日本語 café Привет.mkv`:

  | | |
  |---|---|
  | `QFile::encodeName` | ANSI codepage — `café` becomes `caf\xE9`, and the Japanese and Cyrillic become literal `?`. `avformat_open_input` fails with *Invalid argument*. |
  | `QString::toUtf8` | proper UTF-8. `avformat_open_input` opens the file and reports its 5 streams. |

  So on Windows, subtitle extraction failed outright for any file whose path was not
  representable in the local codepage, and for most non-Latin scripts the encoded bytes are
  `?` and the original is unrecoverable. `QFile::exists()` says yes throughout, because Qt
  keeps the path as UTF-16 and never round-trips it through the codepage — which is why
  this failed specifically at the ffmpeg boundary and nowhere else, and why it never looked
  like a missing file.

  The call site now uses `path.toUtf8()`: ffmpeg wants UTF-8 on Windows, and on Unix
  `QFile::encodeName` already *is* `toUtf8`, so it is not a change there. Verified by
  driving the real extractor against that filename — three tracks and their cues, where the
  same binary built with `encodeName` reports *cannot open ... Invalid argument*.
  `tst_subtitles`' `nonAsciiFilenamesReachTheDecoder` covers it, by copying a fixture to
  that name under a `QTemporaryDir`. The test cannot fail on Linux — encodeName and toUtf8
  agree there — so it is a guard for Windows specifically, and was confirmed to fail
  against a deliberately reverted build before being kept.

Still to judge on Windows, now that there is somewhere to judge them: hwdec, 4K/HEVC and
HDR playback. Trap 22 does not arise there — Mesa's D3D12 driver exists only for WSL.

**Native Linux** needs no porting: the 10-bit workaround in trap 9 disables itself on a
real GPU (`GL_RENDERER` stops matching), and the screenshot tooling is simply replaced by
`grim`/`import`. Nothing is hardcoded for WSL's sake any more — `hwdec` follows the same
software-rasterizer check as the workarounds, so a machine with a render node should pick
up vaapi/nvdec on its own. That is reasoned, not measured: WSL has no `/dev/dri` to try it
against, so **`hwdec-current` on a native Linux desktop is worth a look at port time.**
