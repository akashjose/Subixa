#!/usr/bin/env bash
#
# Is the player actually painting the picture?
#
# Every other check in this repo is headless, deliberately: under WSLg a
# screenshot is the least reliable evidence available. The session can degrade
# into painting black -- or a frozen partial frame -- at every window size while
# mpv logs normally and the transport clock advances, and once it has, every
# visual check returns a false negative until the distro is restarted. That has
# already gone unnoticed for a whole film test.
#
# So this is not a renderer test. It is the precondition to believing one: play a
# known clip, grab two frames a moment apart, and answer three questions with
# numbers rather than with a human squinting at a PNG.
#
#   black    the picture is there at all, rather than an unpainted surface
#   frozen   it changes between grabs -- the degraded state's other face is a
#            single frame repeated forever while the clock keeps moving
#   content  what is on screen resembles what is in the file, compared against
#            the clip's own centre-region colour measured with ffmpeg
#
# Run it before trusting anything visual. If it fails, restart the distro
# (`wsl --terminate Ubuntu-24.04` from Windows) rather than debugging the app.
#
# Usage: tools/render-canary.sh [clip] [reference]
#
# `clip` defaults to testclip.mp4, which is the right one to use: burned-in
# timecode, a picture that changes every frame, and nothing dark in it. A film
# would fail the black check honestly on a studio card.
#
# `reference` is the clip the grabs are compared against, and defaults to the
# clip being played. Passing a different one is how the content check itself
# gets tested -- a canary whose failure path has never run is not evidence.

set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
clip="${1:-$repo/testclip.mp4}"
reference="${2:-$clip}"
binary="$repo/build/custom_media_player"

# The window the canary wants, whatever the developer's own settings say. Also
# keeps the canary from writing into them: it gets its own config and cache.
readonly WINDOW_W=960
readonly WINDOW_H=600

# Tolerances. Generous on colour because the capture path is lossy (see the
# false-colour note in CLAUDE.md); tight on motion because the failure being
# looked for is a *byte identical* repeat.
readonly BLACK_LUMA=16          # below this the pane is unpainted, not dark
readonly COLOUR_TOLERANCE=28    # per-channel, against the clip's own range
readonly MOTION_PIXELS=0.001    # fraction of pixels that must differ at all

die() { printf '\ncanary: %s\n' "$*" >&2; exit 1; }

[[ -x $binary ]] || die "no binary at $binary -- build first"
[[ -f $clip ]] || die "no clip at $clip"
[[ -f $reference ]] || die "no reference clip at $reference"
command -v ffmpeg >/dev/null || die "ffmpeg is needed to measure the frames"

work="$(mktemp -d)"
app_pid=""
cleanup() {
    if [[ -n $app_pid ]] && kill -0 "$app_pid" 2>/dev/null; then
        # SIGTERM, never -9: a hard kill leaves the next instance's audio init
        # timing out for half a minute.
        kill -TERM "$app_pid" 2>/dev/null
        for _ in $(seq 20); do kill -0 "$app_pid" 2>/dev/null || break; sleep 0.25; done
    fi
    rm -rf "$work"
}
trap cleanup EXIT

# ---- a window of known shape ------------------------------------------------
# The player remembers geometry and whether the panel is showing, so the canary
# states both rather than inheriting whatever the last session left. With the
# panel hidden the video pane is the whole client width, which is what lets the
# crop below be a fixed fraction of the capture.
mkdir -p "$work/config/custom_media_player"
cat > "$work/config/custom_media_player/custom_media_player.conf" <<EOF
[ui]
panelVisible=false
panelDetached=false
windowMaximized=false
windowWidth=$WINDOW_W
windowHeight=$WINDOW_H
windowX=60
windowY=60
volume=0
muted=true
darkTheme=true
EOF

log="$work/player.log"
XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache" \
    nohup setsid "$binary" "$clip" > "$log" 2>&1 < /dev/null &
app_pid=$!

# ---- wait for a first frame -------------------------------------------------
# VO: [libmpv] is the one line that says the render path is ours. Anything else
# means mpv made its own window (trap 1) and no amount of pixel comparison will
# be about this app.
for _ in $(seq 60); do
    grep -q "VO: \[libmpv\]" "$log" && break
    sleep 0.5
done
grep -q "VO: \[libmpv\]" "$log" || {
    echo "--- player log ---"; tail -20 "$log"
    die "mpv never reported VO: [libmpv] -- the render path is not the FBO (trap 1)"
}
sleep 3   # let playback settle past the first frames

printf 'canary: %s\n' "$(grep -m1 '^graphics:' "$log" || echo 'graphics: (not logged)')"

# ---- two grabs, a moment apart ----------------------------------------------
temp_win="$(powershell.exe -NoProfile -Command '$env:TEMP' 2>/dev/null | tr -d '\r')"
[[ -n $temp_win ]] || die "cannot reach the Windows side -- this tool only runs under WSLg"
temp_wsl="$(wslpath -u "$temp_win")"

grab() {
    local name="$1"
    powershell.exe -NoProfile -ExecutionPolicy Bypass \
        -File "$(wslpath -w "$repo/tools/wsl-screenshot.ps1")" \
        "$temp_win\\$name" > "$work/$name.out" 2>&1
    grep -q "^OK" "$work/$name.out" || {
        cat "$work/$name.out"
        die "screen capture failed"
    }
    cp "$temp_wsl/$name" "$work/$name"
}

grab canary-a.png
sleep 1.5
grab canary-b.png

# ---- measure ----------------------------------------------------------------
# Crops are the middle of the frame in both the capture and the clip, so the
# comparison does not depend on where the window frame or the transport bar put
# the picture. Chrome is nowhere near the middle 40%.
crop_filter="crop=iw*0.4:ih*0.4:iw*0.3:ih*0.3"

rgb_of() {  # png -> raw rgb24 of its middle
    ffmpeg -v error -i "$1" -vf "$crop_filter,scale=64:64" -f rawvideo -pix_fmt rgb24 -
}

# The clip's own centre colour, per frame, sampled a few times a second. The
# range rather than the mean: a canary that assumed a single value would fail on
# any clip whose picture changes.
ffmpeg -v error -i "$reference" -vf "fps=4,$crop_filter,scale=64:64" \
    -f rawvideo -pix_fmt rgb24 - > "$work/source.rgb" || die "cannot read $reference"

rgb_of "$work/canary-a.png" > "$work/a.rgb" || die "cannot read the first grab"
rgb_of "$work/canary-b.png" > "$work/b.rgb" || die "cannot read the second grab"

python3 - "$work" "$BLACK_LUMA" "$COLOUR_TOLERANCE" "$MOTION_PIXELS" <<'PY'
import sys

work, black_luma, tolerance, motion = sys.argv[1], float(sys.argv[2]), \
    float(sys.argv[3]), float(sys.argv[4])
FRAME = 64 * 64 * 3


def frames(path):
    data = open(path, 'rb').read()
    return [data[i:i + FRAME] for i in range(0, len(data) - FRAME + 1, FRAME)]


def mean_rgb(frame):
    r = sum(frame[0::3]) / (len(frame) / 3)
    g = sum(frame[1::3]) / (len(frame) / 3)
    b = sum(frame[2::3]) / (len(frame) / 3)
    return r, g, b


def luma(rgb):
    return 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]


a = frames(work + '/a.rgb')[0]
b = frames(work + '/b.rgb')[0]
source = frames(work + '/source.rgb')
if not source:
    print('canary: FAIL  the clip yielded no frames to compare against')
    sys.exit(1)

ma, mb = mean_rgb(a), mean_rgb(b)
failures = []

# 1. black -- an unpainted surface rather than a dark shot
print('canary: grabbed  luma %.1f then %.1f, rgb (%.0f,%.0f,%.0f)'
      % (luma(ma), luma(mb), *ma))
if luma(ma) < black_luma and luma(mb) < black_luma:
    failures.append('the pane is black (luma %.1f) -- nothing was painted'
                    % luma(ma))

# 2. frozen -- the degraded state repeats one frame while the clock advances
differing = sum(1 for x, y in zip(a, b) if abs(x - y) > 8) / len(a)
print('canary: motion   %.2f%% of samples changed between grabs' % (differing * 100))
if differing < motion:
    failures.append('the two grabs are identical -- the frame is frozen, which '
                    'is the degraded WSLg state rather than a decode bug')

# 3. content -- does it look like this clip at all
lo = [min(mean_rgb(f)[c] for f in source) for c in range(3)]
hi = [max(mean_rgb(f)[c] for f in source) for c in range(3)]
print('canary: clip     rgb ranges r %.0f-%.0f g %.0f-%.0f b %.0f-%.0f'
      % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
off = [max(lo[c] - ma[c], ma[c] - hi[c], 0) for c in range(3)]
if max(off) > tolerance:
    failures.append('what is on screen does not resemble the clip: channel off '
                    'by %.0f (tolerance %.0f)' % (max(off), tolerance))

if failures:
    print()
    for f in failures:
        print('canary: FAIL  ' + f)
    print('canary: the grabs are kept at %s/canary-{a,b}.png' % work)
    sys.exit(1)

print('canary: PASS  the picture is being painted, and it is this clip')
PY
status=$?

# Keep the evidence when something failed; there is nothing to look at when it
# passed, and the temporary directory goes with the trap.
if [[ $status -ne 0 ]]; then
    keep="$repo/canary-failure"
    mkdir -p "$keep"
    cp "$work"/canary-*.png "$log" "$keep"/ 2>/dev/null
    echo "canary: grabs and log copied to $keep"
fi
exit $status
