#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Akash Jose

# Rewrites testdata/conformance/golden.tsv from the current build.
#
#   SUBIXA_REGOLD=1 ./tools/regold.sh --reason "FFmpeg 8.1 renumbers ASS layers"
#
# Updating a golden is never a side effect of running the suite, and that is the
# whole point of this file existing rather than a --update flag. A golden that
# rewrites itself when it fails does not test anything: the first person to see a
# red re-runs with the flag, the diff scrolls past, and the regression is now the
# expected output. Requiring an explicit variable AND a written reason makes the
# update a decision somebody made, and the reason lands in the file where the
# next reader will see it beside the numbers it explains.
#
# The reason is recorded, not validated. Judging whether "refresh" is a real
# justification is a code review's job, not a shell script's.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD=${SUBIXA_BUILD_DIR:-build}
REASON=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --reason) REASON=${2:-}; shift 2 ;;
        --reason=*) REASON=${1#*=}; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ ${SUBIXA_REGOLD:-} != 1 ]]; then
    cat >&2 <<'EOF'
refusing to rewrite the golden without SUBIXA_REGOLD=1.

A conformance failure means the extractor's output changed. Read the diff first
and decide whether that change is correct; only then record it:

  SUBIXA_REGOLD=1 ./tools/regold.sh --reason "why the new output is right"
EOF
    exit 1
fi

[[ -n $REASON ]] || { echo "--reason is required, and should say why the new output is correct" >&2; exit 2; }

BIN=$BUILD/tst_conformance
[[ -x $BIN ]] || { echo "no $BIN -- build first" >&2; exit 1; }

# The suite renders the corpus itself; asking it to dump rather than compare
# keeps one implementation of the format instead of two that can drift.
GOLDEN=testdata/conformance/golden.tsv
DUMP=$(mktemp)
trap 'rm -f "$DUMP" "$GOLDEN.new"' EXIT

echo "regenerating from $BIN"
SUBIXA_CONFORMANCE_DUMP="$DUMP" QT_QPA_PLATFORM=minimal "$BIN" \
    extractorOutputMatchesTheGolden >/dev/null
[[ -s $DUMP ]] || { echo "the suite produced no output" >&2; exit 1; }

STAMP=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)

# The library versions are NOT read here. tst_conformance writes them into the
# dump itself, from the headers it compiled against and the library it actually
# loaded. Asking pkg-config instead reads whichever .pc file is on the path,
# which reported libavcodec 60 for a binary running against 62 -- provenance
# that is wrong is worse than none, because it is believed.
{
    echo "# Extractor output over testdata/conformance/, asserted byte-identical."
    echo "# Regenerate with tools/regold.sh; never edit by hand."
    echo "#"
    echo "# updated : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "# commit  : $STAMP"
    echo "# reason  : $REASON"
    cat "$DUMP"
} > "$GOLDEN.new"

mv "$GOLDEN.new" "$GOLDEN"

echo "wrote $GOLDEN"
echo
git --no-pager diff --stat -- "$GOLDEN" || true
