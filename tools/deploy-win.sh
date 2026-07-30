#!/usr/bin/env bash
# Stage a runnable Windows bundle from a build-win/ tree into dist/.
#
# This scripts the sequence in docs/windows.md "Deploying" -- that page explains
# *why* each step exists; this file only adds the guards a script needs. Run it
# from the MSYS2 UCRT64 shell: from Git Bash /ucrt64 does not resolve and every
# copy fails.
#
# Usage: tools/deploy-win.sh [build-dir] [dest]
#        defaults:           build-win   dist/subixa-win64
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD=${1:-build-win}
DEST=${2:-dist/subixa-win64}

if [[ ! -d /ucrt64 ]]; then
    echo "error: /ucrt64 does not resolve. Run this from the MSYS2 UCRT64 shell," >&2
    echo "       not Git Bash or MSYS -- see docs/windows.md." >&2
    exit 1
fi
if [[ ! -f "$BUILD/subixa.exe" ]]; then
    echo "error: $BUILD/subixa.exe not found. Build first: cmake --build $BUILD" >&2
    exit 1
fi

mkdir -p "$DEST"
cp "$BUILD/subixa.exe" LICENSE "$DEST/"

# Prove the copy landed, because the failure it guards against is invisible.
# A staging run once stopped before this line while its caller discarded the
# exit status, and $DEST kept a subixa.exe from an earlier build: right DLL
# count, right size, correct playback, and none of the day's work in it. An
# installer was then built from it and verified against the *build* tree rather
# than the staged one, so everything checked passed.
#
# Comparing content rather than timestamps: a copy that silently did not happen
# leaves an older file, and a copy that half-happened leaves a shorter one.
if ! cmp -s "$BUILD/subixa.exe" "$DEST/subixa.exe"; then
    echo "error: $DEST/subixa.exe does not match $BUILD/subixa.exe after copying." >&2
    echo "       The staged bundle would ship a different binary than you built." >&2
    exit 1
fi

# qmlimportscanner lives in share/qt6/bin, not bin, and windeployqt6 looks for
# it beside itself. Without this the scan dies with "Process failed to start"
# and windeployqt6 exits 0 having staged *nothing* -- an empty directory and a
# success code.
export PATH="/ucrt64/share/qt6/bin:$PATH"

windeployqt6 --qmldir qml "$DEST/subixa.exe"

# A success code from windeployqt6 is not evidence it staged anything (above),
# so require the payload it must have produced.
if [[ ! -f "$DEST/Qt6Core.dll" || ! -d "$DEST/qml" ]]; then
    echo "error: windeployqt6 exited 0 but staged no Qt payload into $DEST." >&2
    echo "       Usually qmlimportscanner was not found; check the PATH export above." >&2
    exit 1
fi

# Everything Qt does not own: libmpv, libass, libplacebo, the FFmpeg libraries
# and the codec/support libraries underneath them. Skip-if-present replaces the
# docs' `cp -n` -- same semantics, but its exit status does not depend on the
# coreutils version -- and skipping matters: ldd also reports the Qt DLLs, which
# must not overwrite what windeployqt6 staged.
while IFS= read -r dll; do
    base=$(basename "$dll")
    [[ -e "$DEST/$base" ]] || cp "$dll" "$DEST/"
done < <(ldd "$BUILD/subixa.exe" | grep -oiE '/ucrt64/bin/[^ ]+dll')

# vulkan-1.dll is a load-time dependency of libmpv-2.dll. ldd resolves it out
# of System32, which puts it outside the filter above, and a machine with no
# Vulkan-capable driver has no System32 copy to fall back on either.
cp /ucrt64/bin/vulkan-1.dll "$DEST/"

# MSYS2's Qt keeps its qml tree under share/qt6, windeployqt6's Qt6Core patching
# preserves that offset after relocation, and QML has no application-directory
# fallback the way plugins do. qt.conf reconciles the staged layout with the
# computed one. docs/windows.md has the full story.
cat > "$DEST/qt.conf" <<'EOF'
[Paths]
Prefix = .
Plugins = .
Imports = qml
Qml2Imports = qml
Translations = translations
EOF

echo "Staged: $DEST ($(find "$DEST" -iname '*.dll' | wc -l) DLLs, $(du -sh "$DEST" | cut -f1))"
