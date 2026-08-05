#!/usr/bin/env bash
# Zip an already-staged Windows bundle into the portable download.
#
# The third of the three things a release ships, and the only one that is not
# really a build: tools/deploy-win.sh has already produced a directory that
# runs where it stands, and this wraps it so it can be downloaded. It is
# separate from make-installer-win.sh for the same reason that one is separate
# from deploy-win.sh -- re-running the packaging over a folder you have already
# checked must not mean rebuilding it.
#
# What "portable" means here is only what the staged bundle already is: it runs
# from wherever it is extracted, with no installation step, no uninstall entry
# and no shortcuts. It is *not* portable in the stricter sense of leaving the
# machine untouched -- settings go wherever QSettings puts them, which on
# Windows is the registry, exactly as they do for the installed build.
#
# Needs `zip`, which MSYS2 does not install with the toolchain: pacman -S zip.
#
# Usage: tools/make-portable-win.sh [stage-dir] [out-dir]
#        defaults:                  dist/subixa-win64   dist
set -euo pipefail

cd "$(dirname "$0")/.."

STAGE=${1:-dist/subixa-win64}
OUT=${2:-dist}

if [[ ! -f "$STAGE/subixa.exe" ]]; then
    echo "error: $STAGE/subixa.exe not found. Stage a bundle first:" >&2
    echo "           tools/build-win.sh" >&2
    exit 1
fi

if ! command -v zip >/dev/null; then
    echo "error: zip not found. It is not part of the UCRT64 toolchain:" >&2
    echo "           pacman -S zip" >&2
    exit 1
fi

source "$(dirname "$0")/version.sh"
VERSION=$(subixa_version)

mkdir -p "$OUT"

NAME="Subixa-$VERSION-win64"
ARCHIVE="$OUT/$NAME-portable.zip"
rm -f "$ARCHIVE"

# The archive needs a single versioned directory at its root, so that extracting
# two releases side by side gives two folders rather than one overwriting the
# other -- and the staged directory is named for neither the version nor the
# archive. Renaming it is not an option: make-installer-win.sh takes the same
# path and runs after this one in CI.
#
# So: a hard-linked copy under the name the archive wants. -l because the bundle
# is ~300 MB of DLLs and this costs directory entries rather than a second copy
# of it. It falls back to a real copy on a filesystem that will not link, which
# is slow and still correct.
# Under $OUT rather than in /tmp, and that is the part that makes -l work: MSYS2
# puts /tmp on the system drive while a checkout is routinely on another, and a
# hard link cannot cross a filesystem. From there it would fall back to copying
# 300 MB to save 300 MB.
WORK=$(mktemp -d "$OUT/.portable-XXXXXX")
trap 'rm -rf "$WORK"' EXIT

cp -al "$STAGE" "$WORK/$NAME" 2>/dev/null || cp -r "$STAGE" "$WORK/$NAME"

# -r recurse, -q quiet -- linuxdeploy's output is worth reading and a list of
# 223 DLLs is not. -X drops the extra file attributes, which are a Unix uid/gid
# no Windows tool will ever look at.
( cd "$WORK" && zip -rqX "$NAME.zip" "$NAME" )
mv "$WORK/$NAME.zip" "$ARCHIVE"

[[ -f "$ARCHIVE" ]] || { echo "error: zip exited 0 but produced no archive." >&2; exit 1; }

echo "Portable: $ARCHIVE ($(du -h "$ARCHIVE" | cut -f1))"
