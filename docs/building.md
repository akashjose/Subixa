# Building Subixa

> Linux, in full. For Windows see [`windows.md`](windows.md), which covers the
> MSYS2 toolchain, the build and the deployment sequence together.

## What you need, and why it is not one apt line

| | |
|---|---|
| Qt | 6.12 or newer |
| FFmpeg | 8.1.x |
| mpv | 0.41 (client API 2.5) |
| libplacebo | 7.360 |
| Compiler | C++23 — gcc 13.3 builds it, gcc 14 is preferable |

**No current Linux distribution supplies that stack.** Ubuntu 24.04 ships
FFmpeg 6.1.1, and `apt-cache policy libavcodec-dev` reports the installed
version as the candidate — there is no upgrade. Its Qt is 6.4.2. So two things
are built or fetched outside the package manager: the media libraries from
source, and Qt from `aqtinstall`.

That is more work than `apt install` and it is worth being clear about the cost
before you start: the media stack takes roughly ten minutes to compile on a
twenty-core machine, and Qt is a 1–2 GB download.

The gcc note is exact. The tree uses no C++23 *library* features, so gcc 13.3
compiles it — that is what the reference build here uses. But 13.3's C++23
library is incomplete (`<print>` is absent), so gcc 14 is where that stops being
something to think about.

## 1. Build tooling and headers

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config git curl \
  nasm g++-14 \
  glslang-dev glslang-tools libshaderc-dev spirv-tools liblcms2-dev libunwind-dev \
  libdav1d-dev libxml2-dev libzimg-dev libass-dev libvulkan-dev \
  libpulse-dev libasound2-dev libpipewire-0.3-dev \
  libxrandr-dev libxpresent-dev libxss-dev libxkbcommon-dev libxinerama-dev \
  libegl1-mesa-dev libgl-dev libgbm-dev
```

`tools/build-deps.sh` checks for `meson`, `ninja`, `nasm`, `cmake`,
`pkg-config`, `git`, `curl` and `glslangValidator` before it starts, and prints
this list if one is missing.

## 2. meson and aqtinstall

The distribution's meson is too old for libplacebo, and Qt has to come from
somewhere other than apt. Both are Python tools, and on Ubuntu 24.04
`pip install --user` **fails** — `/usr/lib/python3.12/EXTERNALLY-MANAGED` makes
pip refuse to write into the system interpreter. Use `pipx`:

```bash
sudo apt install -y pipx
pipx install meson
pipx install aqtinstall
pipx ensurepath          # then reopen the shell
```

A virtualenv works equally well. `pip install --user` will only work if you are
already on a pyenv or conda interpreter, which is easy to have and forget about.

## 3. The media stack

```bash
./tools/build-deps.sh                    # installs into ~/data/subixa-stack
```

It builds, in dependency order, **libplacebo 7.360.1**, **FFmpeg 8.1.2** and
**mpv 0.41.0**, at versions pinned in the script. It also installs
**Vulkan-Headers v1.4.321** into the prefix if the system's are older than
1.3.277, because FFmpeg 8.1 requires that and Ubuntu 24.04 ships 1.3.275 — two
patch releases short. Vulkan-Headers ships no pkg-config file, so the script
writes one; without it `pkg-config` keeps answering with the system loader's
older version and FFmpeg's configure fails looking like a missing dependency.

Pass a different prefix as the first argument if you want it elsewhere.

Installing into a prefix rather than `/usr/local` leaves your system's own `mpv`
and `ffmpeg` alone and is reversible with `rm -rf`.

Two things about the FFmpeg configuration, if you change it. `--enable-gpl` is
required and is compatible with a GPL-3.0-or-later application; **never add
`--enable-nonfree`**, which produces a binary that cannot legally be
distributed. And do not reach for `--disable-encoders` to save space:
`MpvEngine::screenshot` writes PNG and JPEG through libavcodec, so a build
without those encoders silently breaks `Ctrl+S` with no diagnostic anywhere.

## 4. Qt

```bash
aqt install-qt linux desktop 6.12.0 linux_gcc_64 \
  -m qtshadertools qtimageformats -O ~/data/Qt
```

**`qtdeclarative` is not a valid module name for Qt 6.** QML, Quick,
QuickControls2 and Svg all ship in the base desktop package, and passing a bad
module name aborts the entire install rather than warning about the one
argument.

## 5. Configure and build

```bash
export CMAKE_PREFIX_PATH="$HOME/data/Qt/6.12.0/gcc_64"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DSUBIXA_DEPS_PREFIX="$HOME/data/subixa-stack"
cmake --build build
./build/subixa /path/to/video.mkv
```

`SUBIXA_DEPS_PREFIX` does two things from one flag: it sets `PKG_CONFIG_PATH`
so the dependency lookup finds the prefix, and it adds an **rpath** so the
binary and every test target find those libraries at run time.

The rpath is the half that is easy to leave out and expensive to debug. Without
it a target links against the prefix and then loads the distribution's older
libraries at run time — worse than failing to link, because it runs. And `ctest`
does not inherit `LD_LIBRARY_PATH` from your shell, so the suites would need one
set by hand forever.

Leave `SUBIXA_DEPS_PREFIX` unset to build against system packages instead. That
works wherever the distribution is current enough, and is what the Windows path
does.

## 6. Tests

Most media fixtures are generated rather than committed, and three suites now
**fail** rather than skip when they are missing — deliberately, because a skip
exits 0 and a checkout that had never generated them used to report green having
asserted almost nothing.

```bash
./testdata/make-fixtures.sh          # embedded, sidecar and shifted-timeline cases
./testdata/make-fixtures.sh --big    # plus a 200k-cue ASS sidecar
cd build && ctest --output-on-failure
```

Seven suites. `testdata/conformance/` is the exception to the generate-locally
rule and is committed as bytes; see [`testing.md`](testing.md).

## Installing

```bash
cmake --install build --prefix /usr/local
```

Note `cmake --install`, not `make install` — every configure line here uses
Ninja, so there is no Makefile in the build tree. It installs:

| | |
|---|---|
| `bin/subixa` | the binary |
| `share/applications/com.akashjose.Subixa.desktop` | the desktop entry |
| `share/icons/hicolor/scalable/apps/com.akashjose.Subixa.svg` | the icon, renamed to the application id |
| `share/doc/subixa/LICENSE` | |

There is no CPack configuration, no `.deb`, no AppImage and no Flatpak.

## Environment switches

All off by default, each disabling one workaround so it can be A/B'd:
`SUBIXA_NO_GPU`, `SUBIXA_NO_SYNC`, `SUBIXA_NO_FBO_CAP`,
`SUBIXA_NO_SUBTITLE_CACHE`, `SUBIXA_HWDEC`. [`graphics.md`](graphics.md) and
[`traps.md`](traps.md) say what each is for.
