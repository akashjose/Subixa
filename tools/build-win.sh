#!/usr/bin/env bash
# Configure, build, test and stage the Windows bundle in one step.
#
# tools/deploy-win.sh stages whatever is already in a build tree, which is all
# it should do -- but on its own it invites two mistakes, and this session made
# both. A bundle was staged from a build tree two days older than the checkout
# and looked entirely current: same DLL count, same size, correct playback,
# with nothing in it from the last two days of commits. Then a second was
# staged from a build with a segfaulting suite, because the deploy had been
# chained on `tail`'s exit status rather than ctest's.
#
# Neither is reachable from here. The build tree is configured from this
# checkout every run, and `set -e` puts ctest between the build and the deploy,
# so a staged bundle is by construction current and green.
#
# Run it from the MSYS2 UCRT64 shell: from Git Bash /ucrt64 does not resolve.
# See docs/windows.md for what each step is doing and why.
#
# Usage: tools/build-win.sh [build-dir] [dest]
#        defaults:          build-win   dist/subixa-win64
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD=${1:-build-win}
DEST=${2:-dist/subixa-win64}

if [[ ! -d /ucrt64 ]]; then
    echo "error: /ucrt64 does not resolve. Run this from the MSYS2 UCRT64 shell," >&2
    echo "       not Git Bash or MSYS -- see docs/windows.md." >&2
    exit 1
fi

# No SUBIXA_DEPS_PREFIX: that flag exists because no Linux distribution ships
# FFmpeg 8, mpv 0.41 and libplacebo 7.3, so the Linux build points CMake at a
# from-source prefix. On Windows pacman supplies exactly those versions, so the
# system paths are the correct ones and the prefix would be wrong.
echo "==> Configuring $BUILD"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release

echo "==> Building"
cmake --build "$BUILD"

# The suites fail rather than skip when their fixtures are absent, which is
# deliberate -- a QSKIP exits 0 and reported six green suites that had asserted
# almost nothing. Generating first keeps that failure meaning "the code is
# wrong" rather than "nobody ran make-fixtures.sh".
if [[ ! -f testdata/subs.mkv ]]; then
    echo "==> Generating fixtures"
    ./testdata/make-fixtures.sh
fi

echo "==> Testing"
ctest --test-dir "$BUILD" --output-on-failure

echo "==> Staging"
exec ./tools/deploy-win.sh "$BUILD" "$DEST"
