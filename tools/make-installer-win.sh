#!/usr/bin/env bash
# Build the Windows installer from an already-staged bundle.
#
# Separate from tools/build-win.sh for the same reason deploy-win.sh is: the
# staging step and the packaging step fail in different ways, and being able to
# re-run the packaging over a folder you have already checked saves rebuilding
# 267 targets to test a change to the .iss.
#
# Needs Inno Setup 6 (ISCC.exe). It is a native Windows tool, not an MSYS2
# package -- `winget install JRSoftware.InnoSetup`, or it is preinstalled on
# GitHub's windows runners.
#
# Usage: tools/make-installer-win.sh [stage-dir] [out-dir]
#        defaults:                   dist/subixa-win64   dist
set -euo pipefail

cd "$(dirname "$0")/.."

STAGE=${1:-dist/subixa-win64}
OUT=${2:-dist}

if [[ ! -f "$STAGE/subixa.exe" ]]; then
    echo "error: $STAGE/subixa.exe not found. Stage a bundle first:" >&2
    echo "           tools/build-win.sh" >&2
    exit 1
fi

# One source for the version, which is the same one the binary's resource block
# is configured from. Reading it here rather than repeating it in the .iss is
# the whole reason this wrapper exists.
VERSION=$(sed -n 's/^project(subixa VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
if [[ -z "$VERSION" ]]; then
    echo "error: could not read the version from CMakeLists.txt." >&2
    exit 1
fi

# Three locations, and the per-user one is not optional: `winget install
# JRSoftware.InnoSetup` puts it under Local AppData by default, which is neither
# Program Files directory. The GitHub runners have it in the x86 one.
#
# The known folder comes from `cygpath -F 28` (CSIDL_LOCAL_APPDATA) rather than
# $LOCALAPPDATA, because an MSYS2 login shell does not inherit it -- the
# variable is set in every Windows process and empty in this one, so reading it
# finds nothing while looking like it tried.
ISCC=""
CANDIDATES=(
    "/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
    "/c/Program Files/Inno Setup 6/ISCC.exe"
    "$(cygpath -F 28 2>/dev/null || true)/Programs/Inno Setup 6/ISCC.exe"
    "$(command -v iscc 2>/dev/null || true)"
)

for candidate in "${CANDIDATES[@]}"; do
    [[ -n "$candidate" && -x "$candidate" ]] && { ISCC=$candidate; break; }
done
if [[ -z "$ISCC" ]]; then
    echo "error: ISCC.exe not found. Install Inno Setup 6:" >&2
    echo "           winget install JRSoftware.InnoSetup" >&2
    exit 1
fi

mkdir -p "$OUT"

# Two conversions, in opposite directions, and both are required.
#
# cygpath -w, because ISCC is a native Windows program and cannot read an MSYS
# path: /f/Subixa/dist reaches it as a relative path off the current drive and
# the compile fails on a directory that looks plausible and is not.
#
# MSYS2_ARG_CONV_EXCL, because MSYS2 rewrites arguments that look like absolute
# POSIX paths when it calls a native program -- and /DAppVersion=0.5.0 looks
# exactly like one. Without this it arrives as a mangled Windows path, ISCC
# counts two script filenames and stops. The failure names neither the switch
# nor the conversion, so it reads as a bug in this script's quoting.
MSYS2_ARG_CONV_EXCL='/D' "$ISCC" \
    "/DAppVersion=$VERSION" \
    "/DStageDir=$(cygpath -w "$PWD/$STAGE")" \
    "/DOutDir=$(cygpath -w "$PWD/$OUT")" \
    "$(cygpath -w "$PWD/tools/subixa.iss")"

INSTALLER="$OUT/subixa-$VERSION-win64-setup.exe"
if [[ ! -f "$INSTALLER" ]]; then
    echo "error: ISCC reported success but $INSTALLER is absent." >&2
    exit 1
fi

echo "Installer: $INSTALLER ($(du -h "$INSTALLER" | cut -f1))"
