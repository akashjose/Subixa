#!/usr/bin/env bash
# Build a relocatable AppImage from a Release tree.
#
# The Linux counterpart of tools/deploy-win.sh, and it exists for the same
# reason that one does: on Windows nothing is installed system-wide, and an
# AppImage takes the same position on Linux by bundling the ldd tree. What it
# is *not* is the counterpart of install-linux.sh, which writes absolute
# rpaths into /usr/local on purpose and produces something that runs on the
# machine that built it and nowhere else.
#
# Why bundling is not optional here: Subixa needs FFmpeg 8, mpv 0.41 and
# libplacebo 7.3, and no distribution ships them. A .deb could not declare
# those dependencies against any archive, so the choice is between bundling
# and not shipping.
#
# Usage: tools/make-appimage.sh [build-dir] [out-dir]
#        defaults:              build-rel   dist
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD=${1:-build-rel}
OUT=${2:-dist}
CACHE=${SUBIXA_APPIMAGE_CACHE:-$HOME/.cache/subixa-appimage}

if [[ ! -x "$BUILD/subixa" ]]; then
    echo "error: $BUILD/subixa not found. Build a Release tree first:" >&2
    echo "           cmake -S . -B $BUILD -G Ninja -DCMAKE_BUILD_TYPE=Release \\" >&2
    echo "                 -DSUBIXA_DEPS_PREFIX=\"\$HOME/data/subixa-stack\"" >&2
    echo "           cmake --build $BUILD" >&2
    exit 1
fi

# One source for the version, the same one the binary is compiled with.
source "$(dirname "$0")/version.sh"
VERSION=$(subixa_version)

QT_PREFIX=${CMAKE_PREFIX_PATH:-$HOME/data/Qt/6.12.0/gcc_64}
DEPS_PREFIX=${SUBIXA_DEPS_PREFIX:-$HOME/data/subixa-stack}
[[ -x "$QT_PREFIX/bin/qmake6" ]] || { echo "error: no qmake6 under $QT_PREFIX" >&2; exit 1; }

# linuxdeploy and its Qt plugin are themselves AppImages, and running one
# normally needs FUSE. EXTRACT_AND_RUN sidesteps that entirely -- it costs a
# temporary unpack per invocation and works in a container, in CI, and in WSL
# where FUSE is present but not always cooperative.
export APPIMAGE_EXTRACT_AND_RUN=1

mkdir -p "$CACHE"
fetch() {
    local name=$1 url=$2
    if [[ ! -x "$CACHE/$name" ]]; then
        echo "==> fetching $name"
        curl -fsSL -o "$CACHE/$name" "$url"
        chmod +x "$CACHE/$name"
    fi
}
fetch linuxdeploy \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage

# Absolute, because DESTDIR below cannot take a relative path and the build
# tree is routinely outside the source tree -- on WSL it lives in ext4 while
# the source stays on the Windows mount, which is the case that breaks a
# "$PWD/$APPDIR" spelling.
APPDIR=$(cd "$BUILD" && pwd)/AppDir
rm -rf "$APPDIR"

# Install rather than copying the binary in by hand, so the AppDir gets the
# desktop entry, the icon and the metainfo from the same rules a real install
# uses. Their absence is what makes an AppImage that runs but integrates with
# nothing.
echo "==> installing into $APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD" --prefix /usr >/dev/null

# The stack has to be findable by name while linuxdeploy walks the ldd tree.
# The installed binary carries an absolute rpath to it, which is how it is
# found -- and linuxdeploy then rewrites every rpath to $ORIGIN, which is the
# whole transformation from "runs here" to "runs anywhere".
export LD_LIBRARY_PATH="$DEPS_PREFIX/lib:$QT_PREFIX/lib:${LD_LIBRARY_PATH:-}"
export QMAKE="$QT_PREFIX/bin/qmake6"

# Without QML_SOURCES_PATHS the Qt plugin deploys the QML *engine* and none of
# the modules the app imports, producing an AppImage that starts and then dies
# on `module "QtQuick" is not installed`.
export QML_SOURCES_PATHS="$PWD/qml"

echo "==> linuxdeploy"
mkdir -p "$OUT"
OUTPUT="$OUT/Subixa-$VERSION-x86_64.AppImage" \
VERSION="$VERSION" \
"$CACHE/linuxdeploy" \
    --appdir "$APPDIR" \
    --executable "$BUILD/subixa" \
    --desktop-file com.akashjose.Subixa.desktop \
    --icon-file icons/subixa.svg \
    --icon-filename com.akashjose.Subixa \
    --plugin qt \
    --output appimage

RESULT="$OUT/Subixa-$VERSION-x86_64.AppImage"
[[ -f "$RESULT" ]] || { echo "error: linuxdeploy exited 0 but produced no AppImage." >&2; exit 1; }
chmod +x "$RESULT"

echo "AppImage: $RESULT ($(du -h "$RESULT" | cut -f1))"
echo
echo "It bundles everything except glibc, so the build host's glibc is the"
echo "floor: $(ldd --version | head -1 | awk '{print $NF}') here. Test it on the oldest system you mean to support."
