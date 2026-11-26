#!/usr/bin/env python3
"""Compare two lossless screenshots and report exactly what moved.

Screen *recordings* are lossy: on a poster-dense page the codec's own noise
swamps a small widget's flicker, and its keyframe cadence looks like a periodic
blink that isn't there. Lossless screenshots (`screencapture -x`, PNG) have no
such noise, so a burst of them plus this script is the reliable way to answer
"does anything change while the app is idle, and where".

usage:
  framediff.py A.png B.png [--grid 24] [--gate 6]

Prints the changed-pixel count, the bounding box in screenshot pixels, and an
ASCII heat map of where the change is concentrated.
"""

import argparse
import subprocess
from array import array


def gray(path, width=None):
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=width,height", "-of", "csv=p=0", path],
        capture_output=True, text=True, check=True).stdout.strip()
    w, h = (int(v) for v in probe.split(",")[:2])
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-pix_fmt", "gray", "-f", "rawvideo", "-"],
        capture_output=True, check=True).stdout
    return array("B", raw), w, h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--grid", type=int, default=24)
    ap.add_argument("--gate", type=int, default=6, help="ignore per-pixel deltas below this")
    args = ap.parse_args()

    pa, w, h = gray(args.a)
    pb, w2, h2 = gray(args.b)
    if (w, h) != (w2, h2):
        raise SystemExit(f"size mismatch: {w}x{h} vs {w2}x{h2}")

    cells = [[0] * args.grid for _ in range(args.grid)]
    changed = 0
    minx, miny, maxx, maxy = w, h, -1, -1
    for y in range(h):
        row = y * w
        crow = cells[y * args.grid // h]
        for x in range(w):
            d = pa[row + x] - pb[row + x]
            if d < 0:
                d = -d
            if d >= args.gate:
                changed += 1
                crow[x * args.grid // w] += 1
                if x < minx:
                    minx = x
                if x > maxx:
                    maxx = x
                if y < miny:
                    miny = y
                if y > maxy:
                    maxy = y

    if changed == 0:
        print(f"identical (gate {args.gate}) -- {w}x{h}")
        return

    print(f"{changed} changed px of {w * h} ({100.0 * changed / (w * h):.2f}%), "
          f"box x={minx} y={miny} w={maxx - minx + 1} h={maxy - miny + 1}")
    peak = max(max(r) for r in cells) or 1
    ramp = " .:-=+*#%@"
    for r in cells:
        print("  |" + "".join(ramp[min(9, int(v / peak * 9))] for v in r) + "|")


if __name__ == "__main__":
    main()
