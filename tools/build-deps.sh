#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Akash Jose

# Builds the media stack Subixa links against, into a prefix of its own.
#
#   ./tools/build-deps.sh [PREFIX]        # default ~/data/subixa-stack
#
# Why this exists rather than `apt install libmpv-dev`: the distribution's
# versions are years behind and cannot be moved. Ubuntu 24.04 LTS ships FFmpeg
# 6.1.1 with no upgrade path in apt, while the project targets 8.1.x -- and the
# Windows/MSYS2 side, which rolls, was already on FFmpeg 8 and Qt 6.11 while
# Linux sat on 2023. That gap is not cosmetic: the subtitle extractor parses
# decoder output, so two platforms on different FFmpeg majors are two platforms
# that can disagree about a cue. See docs/traps.md trap 5.
#
# Installing into a prefix rather than /usr/local leaves the system's own mpv
# and ffmpeg alone, is reversible with `rm -rf`, and is the same shape the
# eventual bundled builds need.
#
# Build order is fixed: libplacebo has no dependency on the others, FFmpeg links
# libplacebo for vf_libplacebo, and mpv links both.
set -euo pipefail

PREFIX=${1:-$HOME/data/subixa-stack}
SRC=${SUBIXA_DEPS_SRC:-$HOME/data/subixa-src}
JOBS=$(nproc)

# Pinned deliberately. A media player whose codec stack drifts is a media player
# whose bug reports cannot be reproduced, so these move as a considered step.
FFMPEG_VERSION=8.1.2
MPV_VERSION=0.41.0
LIBPLACEBO_VERSION=v7.360.1
VULKAN_HEADERS_VERSION=v1.4.321

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
RPATH="-Wl,-rpath,$PREFIX/lib:$PREFIX/lib/x86_64-linux-gnu"

say() { printf '\n=== %s ===\n' "$*"; }

for t in meson ninja nasm cmake pkg-config git curl glslangValidator; do
    command -v "$t" >/dev/null || {
        echo "missing build tool: $t" >&2
        echo "apt: nasm glslang-dev glslang-tools libshaderc-dev liblcms2-dev \\" >&2
        echo "     libdav1d-dev libxml2-dev libzimg-dev libpulse-dev libasound2-dev \\" >&2
        echo "     libpipewire-0.3-dev libxrandr-dev libxpresent-dev libxss-dev \\" >&2
        echo "     libxkbcommon-dev libegl1-mesa-dev libgl-dev libgbm-dev" >&2
        echo "meson comes from pip, not apt, if the distro's is too old." >&2
        exit 1
    }
done

mkdir -p "$PREFIX" "$SRC"

# ---- Vulkan headers -----------------------------------------------------
# Only needed where the distribution's are too old: FFmpeg 8.1 wants >= 1.3.277
# and Ubuntu 24.04 ships 1.3.275, two patch releases short. The headers are
# header-only and the loader is ABI-stable, so newer headers against the
# system's libvulkan.so.1 is the normal arrangement rather than a fudge.
# Vulkan-Headers ships no pkg-config file, so one is written here -- without it
# pkg-config keeps answering with the system loader's older version.
if ! pkg-config --atleast-version=1.3.277 vulkan 2>/dev/null; then
    say "Vulkan headers $VULKAN_HEADERS_VERSION (system's are too old for FFmpeg $FFMPEG_VERSION)"
    [[ -d $SRC/Vulkan-Headers ]] || git clone -q --depth 1 --branch "$VULKAN_HEADERS_VERSION" \
        https://github.com/KhronosGroup/Vulkan-Headers.git "$SRC/Vulkan-Headers"
    cmake -S "$SRC/Vulkan-Headers" -B "$SRC/Vulkan-Headers/build" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$SRC/Vulkan-Headers/build" --target install >/dev/null

    mkdir -p "$PREFIX/lib/pkgconfig"
    cat > "$PREFIX/lib/pkgconfig/vulkan.pc" <<EOF
prefix=$PREFIX
includedir=\${prefix}/include

Name: Vulkan-Headers
Description: Khronos Vulkan headers, used against the system loader
Version: ${VULKAN_HEADERS_VERSION#v}
Cflags: -I\${includedir}
Libs: -lvulkan
EOF
fi

# ---- libplacebo ---------------------------------------------------------
# The renderer mpv 0.41 defaults to: gpu-next is libplacebo, and it is what
# tone mapping, scaling and any future HDR work go through.
say "libplacebo $LIBPLACEBO_VERSION"
[[ -d $SRC/libplacebo ]] || git clone -q --recursive --depth 1 \
    --branch "$LIBPLACEBO_VERSION" https://code.videolan.org/videolan/libplacebo.git "$SRC/libplacebo"
meson setup "$SRC/libplacebo/build" "$SRC/libplacebo" --prefix="$PREFIX" \
    --buildtype=release --wipe \
    -Dvulkan=enabled -Dopengl=enabled -Dshaderc=enabled -Dglslang=enabled \
    -Dlcms=enabled -Ddemos=false -Dtests=false
ninja -C "$SRC/libplacebo/build"
ninja -C "$SRC/libplacebo/build" install

# ---- FFmpeg -------------------------------------------------------------
# GPL and version3 are both fine for a GPL-3.0-or-later application. --enable-
# nonfree is NOT, and is deliberately absent: it produces a binary that cannot
# legally be distributed.
#
# Encoders stay enabled. mpv's `screenshot` command writes PNG and JPEG through
# libavcodec, so a build trimmed with --disable-encoders silently breaks Ctrl+S.
# Trimming belongs with packaging, where the allowlist can be chosen and tested,
# not here where it would quietly remove a feature.
say "FFmpeg $FFMPEG_VERSION"
[[ -f $SRC/ffmpeg-$FFMPEG_VERSION.tar.xz ]] || \
    curl -sL -o "$SRC/ffmpeg-$FFMPEG_VERSION.tar.xz" "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
[[ -d $SRC/ffmpeg-$FFMPEG_VERSION ]] || tar xf "$SRC/ffmpeg-$FFMPEG_VERSION.tar.xz" -C "$SRC"
(
    cd "$SRC/ffmpeg-$FFMPEG_VERSION"
    ./configure --prefix="$PREFIX" --libdir="$PREFIX/lib" \
        --enable-gpl --enable-version3 --enable-shared --disable-static \
        --enable-libdav1d --enable-libplacebo --enable-libxml2 --enable-libzimg \
        --enable-libpulse --enable-vulkan --enable-libass \
        --disable-doc --disable-debug \
        --extra-cflags="-I$PREFIX/include" --extra-ldflags="$RPATH"
    make -j"$JOBS"
    make install
)

# ---- mpv ----------------------------------------------------------------
# cplayer is kept so there is a reference player at exactly the version libmpv
# is, which is the quickest way to tell a Subixa bug from an mpv one. lua and
# javascript are off: Subixa drives mpv through the client API and never loads a
# script, so they are payload without a caller.
say "mpv $MPV_VERSION"
[[ -f $SRC/mpv-$MPV_VERSION.tar.gz ]] || \
    curl -sL -o "$SRC/mpv-$MPV_VERSION.tar.gz" \
        "https://github.com/mpv-player/mpv/archive/refs/tags/v$MPV_VERSION.tar.gz"
[[ -d $SRC/mpv-$MPV_VERSION ]] || tar xf "$SRC/mpv-$MPV_VERSION.tar.gz" -C "$SRC"
LDFLAGS="$RPATH" meson setup "$SRC/mpv-$MPV_VERSION/build" "$SRC/mpv-$MPV_VERSION" \
    --prefix="$PREFIX" --libdir="$PREFIX/lib" --buildtype=release --wipe \
    -Dlibmpv=true -Dcplayer=true -Dgpl=true \
    -Dvulkan=enabled -Degl=enabled -Dx11=enabled -Dwayland=enabled \
    -Dpulse=enabled -Dalsa=enabled -Dpipewire=enabled \
    -Dlua=disabled -Djavascript=disabled
ninja -C "$SRC/mpv-$MPV_VERSION/build"
ninja -C "$SRC/mpv-$MPV_VERSION/build" install

# ---- report -------------------------------------------------------------
say "installed"
for p in mpv libplacebo libavcodec libavformat libavutil; do
    printf '  %-14s %s\n' "$p" "$(pkg-config --modversion "$p")"
done

cat <<EOF

Build Subixa against it with:

  export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib/x86_64-linux-gnu/pkgconfig"
  export CMAKE_PREFIX_PATH="\$HOME/data/Qt/6.12.0/gcc_64"
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \\
        -DCMAKE_EXE_LINKER_FLAGS="$RPATH"

The rpath is what lets the binary and the six test suites find this stack
without LD_LIBRARY_PATH set in the environment, which matters because ctest
does not inherit one.
EOF
