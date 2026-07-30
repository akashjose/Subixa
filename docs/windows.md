# Windows: toolchain, build and deployment

> Moved out of `README.md`, which is now a page for someone deciding whether to
> use Subixa rather than a record of how it is put together. Nothing here was
> rewritten — the deployment sequence and its comments are verbatim, because
> each comment is a debugging cycle somebody already paid for.

## Why MSYS2 and gcc rather than MSVC

Built under **MSYS2 UCRT64**, with gcc rather than MSVC. The reason is libmpv:
there is no MSVC build of it to link against, so that toolchain means vcpkg or a
hand-generated import library for a downloaded `libmpv-2.dll`
([`graphics.md`](graphics.md) weighs this up). MSYS2 packages libmpv, FFmpeg and
Qt 6 against one another already, and everything it produces is an ordinary
native Windows binary — the MSYS2 shell is the build environment, not a runtime
dependency of the player.

MSYS2 rolls, so unlike Linux it reaches the required versions on its own. But it
has to be brought up to date first. **Update before installing.** A prefix below
the Windows floor will fail `find_package` outright, which is the intended
behaviour and not a build error to work around.

That floor is **Qt 6.11**, one release below the Linux one, and the split is
deliberate. MSYS2 supplies Qt *and* the media stack from a single repository, so
a floor its `qt6-base` cannot meet does not make the Windows build older — it
makes it unconfigurable, which is how a bundle once came to be staged from a
build tree two days behind the checkout while looking entirely current.
`CMakeLists.txt` carries the full reasoning. Raise this side the day MSYS2
ships 6.12.

```bash
pacman -Syu                              # then reopen the shell and run it again
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-svg mingw-w64-ucrt-x86_64-qt6-shadertools \
  mingw-w64-ucrt-x86_64-mpv mingw-w64-ucrt-x86_64-ffmpeg
```

`tools/build-deps.sh` and `SUBIXA_DEPS_PREFIX` are Linux-only and are not wanted
here: MSYS2 already packages the stack against itself, which is the problem that
script exists to solve on a distribution that does not.

## Building

From the **UCRT64** shell specifically — not MSYS, not MINGW64. The compiler,
CMake and Qt all have to come out of the same prefix, and MSYS2's own `cmake`
package is a native Windows CMake that already searches `/ucrt64`, so no
`CMAKE_PREFIX_PATH` is needed.

**`tools/build-win.sh` is the command to run.** It configures, builds, generates
the fixtures if they are absent, runs the ten suites and stages the bundle —
and `set -e` puts `ctest` between the build and the deploy, so nothing can be
staged from a tree that does not pass.

```bash
./tools/build-win.sh                  # build-win/ -> dist/subixa-win64/
```

By hand, which is what that script does:

```bash
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
./build-win/subixa.exe /path/to/video.mkv
```

The build directory is `build-win/` rather than `build/` so that one checkout can
carry a Linux and a Windows tree at once without either overwriting the other's
cache. Both are gitignored, as is `dist/`.

`pkg-config` is deliberately unused on this path. Under MSYS2 the `.pc` files
hardcode `prefix=/ucrt64`, so a native CMake is handed `-I/ucrt64/include` — an
MSYS path with no drive letter — and the generate step fails on a non-existent
include directory. libmpv and FFmpeg are located with `find_path`/`find_library`
instead, which also means a Windows build works against a plain libmpv SDK that
ships no `.pc` files at all. The full reasoning is in the comment above
`subixa_import_library` in `CMakeLists.txt`.

### Last recorded build

**2026-07-31**, from a clean checkout through `tools/build-win.sh`: 267 targets
warning-free under `-Wall -Wextra`, ten suites green, bundle staged at 223 DLLs
and 298 MB.

| | |
|---|---|
| Toolchain | MSYS2 UCRT64, gcc 16.1.0, CMake 4.4.0, Ninja 1.13.2 |
| Qt | 6.11.1 — the Windows floor |
| libmpv | 0.41.0 |
| FFmpeg | 8.1.2 |

The run before this one was 2026-07-28, and the gap is worth recording rather
than overwriting: for those three days the Qt floor sat at 6.12, which MSYS2
could not satisfy, so `build-win/` could not be reconfigured at all. It went on
producing a bundle that passed every check anyone thought to run — right DLL
count, right size, correct playback — while containing none of the work
committed in the meantime. What caught it was the *absence* of a
`meta/schemaVersion` key in the registry, not anything about the bundle itself.

## Deploying

Scripted as `tools/deploy-win.sh`, which runs the sequence below plus the
guards a script needs — chiefly refusing to continue when `windeployqt6` stages
nothing. The listing stays here because the *reasons* live in its comments.

`tools/build-win.sh` calls it as its last step, and that is the normal way to
reach it. Run it directly only to re-stage a build tree you already know is
current — it stages whatever it is pointed at, which is the right behaviour for
a packer and the reason a stale tree once shipped unnoticed.

`windeployqt6` handles Qt and nothing else. libmpv's dependency tree is the
larger half of the payload and has to be walked separately.

```bash
DEST=dist/subixa-win64
mkdir -p "$DEST" && cp build-win/subixa.exe LICENSE "$DEST/"

# qmlimportscanner lives in share/qt6/bin, not bin, and windeployqt6 looks for it
# beside itself. Without this the scan dies with "Process failed to start", and
# windeployqt6 then exits 0 having staged *nothing* -- an empty directory and a
# success code.
export PATH="/ucrt64/share/qt6/bin:$PATH"

# Qt: libraries, the platform/imageformat/TLS plugins, and the QML modules the
# app imports. --qmldir is what makes it read the imports rather than guess.
windeployqt6 --qmldir qml "$DEST/subixa.exe"

# Everything else: libmpv, libass, libplacebo, the FFmpeg libraries, and the
# ~140 codec and support libraries underneath them.
ldd build-win/subixa.exe | grep -oiE '/ucrt64/bin/[^ ]+dll' | xargs -I{} cp -n {} "$DEST/"

# vulkan-1.dll is a load-time dependency of libmpv-2.dll, so the player will not
# start without it. `ldd` resolves it out of System32, which puts it outside the
# filter above -- and a machine with no Vulkan-capable driver has no System32
# copy to fall back on either.
cp /ucrt64/bin/vulkan-1.dll "$DEST/"

# windeployqt does not write a qt.conf, and does not need to on a stock Qt: it
# patches Qt6Core so the prefix relocates to wherever the DLL now sits, and the
# layout it stages into is the one a stock Qt would then compute. That patching
# works fine here -- the deployed Qt6Core reports a prefix of $DEST exactly.
#
# What does not survive is the *rest* of the layout. MSYS2 configures Qt with its
# qml and plugin trees under share/qt6 rather than directly under the prefix, and
# relocation keeps that offset, so the bundle resolves imports to
# $DEST/share/qt6/qml while windeployqt has staged them to $DEST/qml. Plugins
# disagree the same way and get away with it, because Qt always searches the
# application directory for those; QML has no such fallback, so QML is what
# breaks. qt.conf is what reconciles the two layouts.
cat > "$DEST/qt.conf" <<'EOF'
[Paths]
Prefix = .
Plugins = .
Imports = qml
Qml2Imports = qml
Translations = translations
EOF
```

`cp -n` matters in the `ldd` command: it also reports the Qt DLLs, and without it
they would be copied back over whatever `windeployqt6` had just staged. The
result is roughly 170 DLLs and about 300 MB. There is no installer, and nothing
is code-signed.

The paths above are MSYS2 mount paths, so run this from the **UCRT64 shell**.
From Git Bash `/ucrt64` does not resolve and every `cp` fails with "No such file
or directory"; the equivalent there is `/c/msys64/ucrt64`.

Verified on 2026-07-28 by running the staged bundle with `PATH` cut down to
`C:\Windows\system32;C:\Windows`: it starts, finds the Intel UHD 770 through the
real driver, and decodes with `d3d11va-copy` through `wasapi`. That is standalone
on *this* machine with MSYS2 merely off the PATH, which is a weaker claim than a
clean machine — still untried — but it is the check that turned up the missing
`qt.conf`.

Roughly 40 MB of that payload is video **encoders** — `libx265`, `libaom`,
`libSvtAv1Enc`, `libx264` — which a player never uses, and another ~20 MB is
transitive over-pull including `libpython3.14.dll`. Trimming belongs with
packaging rather than here, and must keep the `png` and `mjpeg` encoders:
`MpvEngine::screenshot` writes through libavcodec, so a build trimmed with
`--disable-encoders` silently breaks `Ctrl+S`.

## Differences worth knowing

- **The player is linked as a GUI binary**, so Windows gives it no console and Qt
  would ordinarily send `qInfo()` to the debugger. It attaches to the parent
  console when it has one, so launching from a terminal still prints the
  `GL_RENDERER` and hwdec lines — the two most useful when an install misbehaves
  — while a double-click stays windowless. `QT_FORCE_STDERR_LOGGING=1` is the
  fallback if they go missing.
- **The test suites build and run unchanged**: `cd build-win && ctest
  --output-on-failure`. Six passed here on 2026-07-26 — a record that predates
  `tst_conformance`, so it covers six of the current ten. They are left as
  console programs on purpose, since ctest reads their stdout. Two needed
  portability fixes to get there: `tst_qmlpanel` was reaching the developer's
  real registry and profile rather than a temporary one, and `tst_mpvtracks` has
  to re-issue a seek that libmpv 0.41 does not always act on while paused. Both
  are documented at the sites.
- **Trap 22 is a Mesa D3D12 bug and does not arise here**, since that driver
  exists only under WSLg. `AppText` still picks between the two text paths at
  runtime from the real `GL_RENDERER`, so nothing in the QML changes either way.
- **A non-ASCII path once broke subtitle extraction**, because `QFile::encodeName`
  is the local codepage on Windows and `avformat_open_input` refuses what it
  produces. Fixed by passing `toUtf8()` at the call site, with a regression test.
