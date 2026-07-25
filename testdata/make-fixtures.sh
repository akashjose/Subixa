#!/usr/bin/env bash
# Regenerates the subtitle test fixtures. Only the small .srt/.ass sources are
# checked in; everything with video in it is built here, because testclip.mp4 is
# 6 MB and the muxed variants are multiples of that.
#
#   ./testdata/make-fixtures.sh          # normal fixtures
#   ./testdata/make-fixtures.sh --big    # plus the 200k-cue stress file (27 MB)
#
# What each one is for:
#   subs.mkv      three embedded tracks (subrip, ass, subrip) with language tags
#   movtext.mp4   the mp4-native text codec, which decodes differently
#   shifted.mkv   every stream offset by 1 h -- timestamps must rebase to zero
#   shifted.mp4   video at 1 h but subtitles still at 0 -- must NOT rebase
#   sidecar.mp4   no embedded subs; .srt/.ass/.fr.srt files sit next to it
#   huge.mp4      200k-cue ASS sidecar, for checking the parse stays off the GUI
set -euo pipefail

cd "$(dirname "$0")"
CLIP=../testclip.mp4

if [[ ! -f $CLIP ]]; then
    echo "missing $CLIP -- generate it first:" >&2
    echo "  ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=30:duration=15 \\" >&2
    echo "         -f lavfi -i sine=frequency=440:duration=15 \\" >&2
    echo "         -c:v libx264 -c:a aac testclip.mp4" >&2
    exit 1
fi

q=(-y -loglevel error)

echo "subs.mkv"
ffmpeg "${q[@]}" -i "$CLIP" -i en.srt -i styled.ass -i fr.srt \
    -map 0:v -map 0:a -map 1 -map 2 -map 3 -c:v copy -c:a copy -c:s copy \
    -metadata:s:s:0 language=eng -metadata:s:s:0 title="English SRT" \
    -metadata:s:s:1 language=jpn -metadata:s:s:1 title="Styled ASS" \
    -metadata:s:s:2 language=fre -metadata:s:s:2 title="French SRT" \
    subs.mkv

echo "movtext.mp4"
ffmpeg "${q[@]}" -i "$CLIP" -i en.srt -map 0:v -map 0:a -map 1 \
    -c:v copy -c:a copy -c:s mov_text -metadata:s:s:0 language=eng movtext.mp4

# -itsoffset shifts every stream in matroska, so the container start time and the
# subtitle timeline agree: the rebase should fire.
echo "shifted.mkv"
ffmpeg "${q[@]}" -itsoffset 3600 -i subs.mkv -map 0 -c copy shifted.mkv

# mp4 offsets via edit lists, which -itsoffset does not apply to the text track.
# The result is a container claiming to start at 1 h with subtitles still at 0 --
# exactly the case where a blanket rebase would flatten every cue onto 00:00:00.
echo "shifted.mp4"
ffmpeg "${q[@]}" -itsoffset 3600 -i movtext.mp4 -map 0 -c copy shifted.mp4

echo "sidecar.mp4 + sidecars"
cp "$CLIP" sidecar.mp4
cp styled.ass sidecar.ass      # no language tag in the name
cp en.srt sidecar.srt          # ditto
cp fr.srt sidecar.fr.srt       # language comes from the filename

if [[ ${1:-} == --big ]]; then
    echo "huge.mp4 + huge.en.ass (200k cues)"
    cp "$CLIP" huge.mp4
    python3 - <<'PY'
def ts(ms):
    return f"{ms//3600000}:{(ms//60000)%60:02d}:{(ms//1000)%60:02d}.{(ms%1000)//10:02d}"

header = open("styled.ass").read().split("Dialogue:")[0]
with open("huge.en.ass", "w") as f:
    f.write(header)
    for i in range(200_000):
        start = i * 70
        f.write(f"Dialogue: 0,{ts(start)},{ts(start + 65)},Default,,0,0,0,,"
                f"{{\\pos(640,600)\\fad(150,150)}}Cue {i} with \\Nbreak and "
                f"{{\\i1}}emphasis{{\\i0}} &amp; entity.\n")
PY
fi

echo "done"
