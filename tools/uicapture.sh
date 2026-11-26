#!/bin/zsh
# Capture the running cutefin window, for eyeballing the UI and for hunting
# visual glitches. macOS only (screencapture + CoreGraphics events).
#
#   uicapture.sh shot   OUT.png              one lossless PNG of the window
#   uicapture.sh burst  DIR N                N lossless PNGs, back to back
#   uicapture.sh record OUT.mov SECONDS      screen recording (whole screen)
#   uicapture.sh rect                        print the window rect + crop string
#
# Why both stills and video: a recording is lossy, and on a poster-dense page
# the codec's noise and keyframe cadence read as flicker that isn't there. Use
# `record` + tools/flickerscan.py to find *when* something happens, then
# `burst` + tools/framediff.py to prove what actually changed -- lossless PNGs
# of an idle app that differ at all are hard evidence of a repaint loop.
set -e

here=${0:A:h}

rect() {
    osascript -e 'tell application "System Events" to tell process "cutefin" to get {position, size} of window 1' \
        | tr -d ' '
}

focus() {
    osascript -e 'tell application "System Events" to tell process "cutefin" to set frontmost to true' >/dev/null
    sleep 0.4
}

case "$1" in
rect)
    r=$(rect)
    x=${r[(ws:,:)1]}; y=${r[(ws:,:)2]}; w=${r[(ws:,:)3]}; h=${r[(ws:,:)4]}
    print "window (points): x=$x y=$y w=$w h=$h"
    # screencapture -R takes points; a Retina recording is 2x, so flickerscan's
    # --crop (recording pixels) needs the doubled rect.
    print "screencapture -R: $x,$y,$w,$h"
    print "flickerscan --crop: $((w*2)):$((h*2)):$((x*2)):$((y*2))"
    ;;
shot)
    out=${2:?usage: uicapture.sh shot OUT.png}
    focus
    r=$(rect)
    rm -f "$out"
    screencapture -x -o -R"${r[(ws:,:)1]},${r[(ws:,:)2]},${r[(ws:,:)3]},${r[(ws:,:)4]}" "$out"
    print "wrote $out"
    ;;
burst)
    dir=${2:?usage: uicapture.sh burst DIR N}
    n=${3:-12}
    focus
    r=$(rect)
    mkdir -p "$dir"
    for i in $(seq -w 1 $n); do
        rm -f "$dir/b$i.png"
        screencapture -x -o -R"${r[(ws:,:)1]},${r[(ws:,:)2]},${r[(ws:,:)3]},${r[(ws:,:)4]}" "$dir/b$i.png"
    done
    print "wrote $n frames to $dir"
    # Identical hashes repeating non-consecutively mean the UI is oscillating
    # between states rather than settling.
    for f in "$dir"/*.png; do
        printf "%s " "$f"
        ffmpeg -v error -i "$f" -pix_fmt gray -f rawvideo - 2>/dev/null | md5
    done
    ;;
record)
    out=${2:?usage: uicapture.sh record OUT.mov SECONDS}
    secs=${3:-12}
    focus
    rm -f "$out"          # screencapture refuses to overwrite
    screencapture -x -V "$secs" "$out"
    print "wrote $out"
    ;;
*)
    sed -n '2,12p' "$here/uicapture.sh"
    exit 2
    ;;
esac
