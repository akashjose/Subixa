#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Akash Jose

# Regenerates the conformance corpus. Unlike testdata/make-fixtures.sh, whose
# output is gitignored, **everything this produces is committed as bytes**.
#
#   ./testdata/conformance/make-conformance.sh
#
# Why committed rather than generated at test time: `git ls-files testdata/` used
# to return four files, so every developer and every CI runner parsed containers
# muxed by whatever ffmpeg happened to be installed. Two platforms agreeing then
# proved nothing -- each had decoded its own inputs. A committed corpus is the
# only arrangement where "the Linux leg and the Windows leg agree" is evidence.
#
# Every file here is a few kilobytes. The video is a 32x32 two-second token
# stream, present only because a subtitle-only container reports start_time =
# N/A, which would leave readContainer's rebase branch permanently dead and
# silently retire the trap 6 cases.
#
# Determinism matters and is easy to get wrong: `-fflags +bitexact` must be an
# **output** option. Before `-i` it applies to the input demuxer, and the
# matroska muxer goes on writing a random SegmentUID -- two runs a second apart
# then differ by SHA-256 and every regeneration looks like a change.
#
# libx264 is deliberately not used. A player-focused FFmpeg has no external video
# encoders, so a fixture step that reaches for one only works against whichever
# ffmpeg happens to be on PATH. mpeg4 is native and always present.
set -euo pipefail

cd "$(dirname "$0")"
SRC=src
FF=${SUBIXA_FFMPEG:-ffmpeg}

command -v "$FF" >/dev/null || { echo "no ffmpeg on PATH; set SUBIXA_FFMPEG" >&2; exit 1; }

q=(-y -loglevel error)
bitexact=(-fflags +bitexact)

echo "clip.mp4 (token video)"
"$FF" "${q[@]}" -f lavfi -i "testsrc2=size=32x32:rate=5:duration=2" \
    -c:v mpeg4 -t 2 "${bitexact[@]}" clip.mp4

# Three text tracks with language tags, in container stream order. The eng track
# is SRT, the jpn one ASS with a styles table and override tags, the fre one SRT.
echo "basic.mkv (3 text tracks)"
"$FF" "${q[@]}" -i clip.mp4 -i $SRC/basic.en.srt -i $SRC/styled.jpn.ass -i $SRC/basic.fr.srt \
    -map 0:v -map 1 -map 2 -map 3 -c:v copy -c:s copy \
    -metadata:s:s:0 language=eng -metadata:s:s:0 title="English SRT" \
    -metadata:s:s:1 language=jpn -metadata:s:s:1 title="Styled ASS" \
    -metadata:s:s:2 language=fre -metadata:s:s:2 title="French SRT" \
    "${bitexact[@]}" basic.mkv

# Entities the decoder does NOT resolve, so the browser must (traps 7 and 12).
echo "entities.mkv"
"$FF" "${q[@]}" -i clip.mp4 -i $SRC/entities.en.srt -map 0:v -map 1 \
    -c:v copy -c:s copy -metadata:s:s:0 language=eng "${bitexact[@]}" entities.mkv

# Styling said entirely through the [V4+ Styles] table: not one cue in it carries
# an override tag, which is how most professionally authored ASS is written. Read
# without the table it comes out plain in the browser while libass draws it
# italic, bold and coloured over the picture -- the two halves of the reader's
# screen disagreeing. The last cue names a style the table does not declare,
# which has to render as plain text rather than fail.
echo "styletable.mkv (styles table, no override tags)"
"$FF" "${q[@]}" -i clip.mp4 -i $SRC/styletable.en.ass -map 0:v -map 1 \
    -c:v copy -c:s copy -metadata:s:s:0 language=eng \
    -metadata:s:s:0 title="Table styled ASS" "${bitexact[@]}" styletable.mkv

# A cue that is a vector shape rather than text (trap 14).
echo "drawing.mkv"
"$FF" "${q[@]}" -i clip.mp4 -i $SRC/drawing.en.ass -map 0:v -map 1 \
    -c:v copy -c:s copy -metadata:s:s:0 language=eng "${bitexact[@]}" drawing.mkv

# -itsoffset shifts every stream in matroska, so the container start time and the
# subtitle timeline agree and the rebase must fire (trap 6, positive case).
echo "shifted.mkv"
"$FF" "${q[@]}" -itsoffset 3600 -i basic.mkv -map 0 -c copy "${bitexact[@]}" shifted.mkv

# mp4 offsets via edit lists, which -itsoffset does not apply to the text track:
# the container claims to start at 1 h while the subtitles are still at 0. A
# blanket rebase would flatten every cue onto 00:00:00 (trap 6, negative case).
echo "noshift.mp4"
"$FF" "${q[@]}" -i clip.mp4 -i $SRC/basic.en.srt -map 0:v -map 1 \
    -c:v copy -c:s mov_text -metadata:s:s:0 language=eng "${bitexact[@]}" movtext.mp4
"$FF" "${q[@]}" -itsoffset 3600 -i movtext.mp4 -map 0 -c copy "${bitexact[@]}" noshift.mp4

# Eight subtitle tracks. SubtitlePanel gates on `tracks.length <= 6` to choose
# tabs or the picker, and every other fixture in the tree has 1 or 3 -- so the
# picker branch, which is what the 65-track films actually render, has never been
# instantiated by any test. A few kilobytes flips it.
echo "manytracks.mkv (8 text tracks)"
args=(-i clip.mp4)
for i in $(seq 1 8); do args+=(-i $SRC/basic.en.srt); done
maps=(-map 0:v)
for i in $(seq 1 8); do maps+=(-map "$i"); done
meta=()
langs=(eng fre deu spa ita jpn kor zho)
for i in $(seq 0 7); do
    meta+=(-metadata:s:s:$i "language=${langs[$i]}")
    meta+=(-metadata:s:s:$i "title=Track $((i + 1))")
done
"$FF" "${q[@]}" "${args[@]}" "${maps[@]}" -c:v copy -c:s copy "${meta[@]}" \
    "${bitexact[@]}" manytracks.mkv

# No embedded subtitles; the sidecars sit beside it and are found by name.
echo "sidecar.mp4 + sidecars"
"$FF" "${q[@]}" -i clip.mp4 -map 0:v -c:v copy "${bitexact[@]}" sidecar.mp4
cp $SRC/basic.en.srt sidecar.srt
cp $SRC/basic.fr.srt sidecar.fr.srt

rm -f movtext.mp4
echo
echo "corpus:"
ls -1 *.mkv *.mp4 *.srt 2>/dev/null | while read -r f; do
    printf '  %-20s %6s bytes  %s\n' "$f" "$(stat -c%s "$f")" "$(sha256sum "$f" | cut -c1-12)"
done
printf '  %-20s %6s bytes total\n' "" "$(du -sb . | cut -f1)"
