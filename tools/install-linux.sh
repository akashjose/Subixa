#!/usr/bin/env bash
# Build a Release tree and update the installed copy. The Linux counterpart of
# tools/deploy-win.sh, for the same reason that script exists: "rebuild and
# reinstall" was a documented command sequence, and a sequence gets skipped or
# half-remembered where a script does not.
#
# Usage: tools/install-linux.sh [prefix]     (default /usr/local)
#
# Asks for sudo only if the prefix is not writable, and only for the install
# step -- the build itself must never run as root.
#
# Qt and the media stack come from the documented locations, overridable the
# same way the build is: CMAKE_PREFIX_PATH for Qt, SUBIXA_DEPS_PREFIX for the
# stack (empty string = system packages).
set -euo pipefail

cd "$(dirname "$0")/.."

PREFIX=${1:-/usr/local}
: "${CMAKE_PREFIX_PATH:=$HOME/data/Qt/6.12.0/gcc_64}"
export CMAKE_PREFIX_PATH

DEPS=${SUBIXA_DEPS_PREFIX-$HOME/data/subixa-stack}

# Decide about sudo before spending minutes on the build. Without a terminal
# sudo cannot ask for a password (unless its credentials are still cached), and
# finding that out after the build is the annoying order to find it out in.
SUDO=""
if [[ ! -w "$PREFIX" ]]; then
    SUDO=sudo
    if [[ ! -t 0 ]] && ! sudo -n true 2>/dev/null; then
        echo "error: installing to $PREFIX needs sudo, and there is no" >&2
        echo "       terminal to ask for the password on. Run this from a" >&2
        echo "       terminal, or pass a writable prefix instead:" >&2
        echo "           tools/install-linux.sh ~/.local" >&2
        exit 1
    fi
fi

# Release, tests off: this tree exists to be installed, not to develop in.
# ctest belongs to build/, which this script deliberately does not touch.
cmake -S . -B build-rel -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSUBIXA_BUILD_TESTS=OFF \
    -DSUBIXA_DEPS_PREFIX="$DEPS"
cmake --build build-rel

[[ -z "$SUDO" ]] || echo "== $PREFIX is not writable; installing with sudo =="
$SUDO cmake --install build-rel --prefix "$PREFIX"

# Files from an install made before the application-id rename sit under the old
# names and would give the launcher two entries. Remove them if present.
for stale in "$PREFIX/share/applications/subixa.desktop" \
             "$PREFIX/share/icons/hicolor/scalable/apps/subixa.svg"; do
    if [[ -e "$stale" ]]; then
        echo "== removing pre-rename leftover: $stale =="
        $SUDO rm "$stale"
    fi
done

echo "== installed: $("$PREFIX/bin/subixa" --version) at $PREFIX/bin/subixa =="
