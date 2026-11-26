#!/usr/bin/env python3
"""Locate visual glitches (flashes, jumps, blinking widgets) in a screen recording.

Decodes the recording to small grayscale frames via ffmpeg, then reports, for
every frame whose content differs from the previous one, how much changed and
*where* -- as a bounding box in window coordinates plus an ASCII heat map. That
"where" is the part a plain scene-change detector never tells you, and it is
what turns "something flickers" into "the chevron at (x, y) blinks every 4
frames".

usage:
  flickerscan.py REC.mov [--crop W:H:X:Y] [--threshold 1.0] [--top 25]
                          [--grid 24] [--from SEC] [--to SEC]

--crop is in *recording pixels* (a Retina capture is 2x the window's point
size); pass the window rect so reported coordinates are window-relative.
Requires only ffmpeg and the standard library.
"""

import argparse
import subprocess
import sys
from array import array

WIDTH = 320  # analysis resolution; enough to localise a 32px widget


def decode(path, crop, from_sec, to_sec):
    """Yield (index, gray8 bytes, w, h) for each frame, downscaled to WIDTH."""
    vf = []
    if crop:
        vf.append("crop=" + crop)
    vf.append(f"scale={WIDTH}:-2")
    cmd = ["ffmpeg", "-v", "error"]
    if from_sec:
        cmd += ["-ss", str(from_sec)]
    if to_sec:
        cmd += ["-to", str(to_sec)]
    cmd += ["-i", path, "-vf", ",".join(vf), "-pix_fmt", "gray", "-f", "rawvideo", "-"]

    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
         "stream=width,height", "-of", "csv=p=0", path],
        capture_output=True, text=True, check=True).stdout.strip()
    src_w, src_h = (int(v) for v in probe.split(",")[:2])
    if crop:
        cw, ch = (int(v) for v in crop.split(":")[:2])
    else:
        cw, ch = src_w, src_h
    h = int(round(WIDTH * ch / cw / 2)) * 2

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    size = WIDTH * h
    index = 0
    while True:
        buf = proc.stdout.read(size)
        if len(buf) < size:
            break
        yield index, buf, WIDTH, h
        index += 1
    proc.stdout.close()
    proc.wait()


def frame_report(prev, cur, w, h, grid):
    """Mean abs diff plus the bounding box and per-cell heat of what changed."""
    cells = [[0] * grid for _ in range(grid)]
    total = 0
    minx, miny, maxx, maxy = w, h, -1, -1
    for y in range(h):
        row = y * w
        gy = y * grid // h
        crow = cells[gy]
        for x in range(w):
            d = prev[row + x] - cur[row + x]
            if d < 0:
                d = -d
            if d > 8:  # ignore compression noise
                total += d
                crow[x * grid // w] += d
                if x < minx:
                    minx = x
                if x > maxx:
                    maxx = x
                if y < miny:
                    miny = y
                if y > maxy:
                    maxy = y
    return total / (w * h), cells, (minx, miny, maxx, maxy)


def heat_map(cells, grid):
    peak = max(max(row) for row in cells) or 1
    ramp = " .:-=+*#%@"
    return ["".join(ramp[min(len(ramp) - 1, int(v / peak * (len(ramp) - 1)))] for v in row)
            for row in cells]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("recording")
    ap.add_argument("--crop", default=None, help="W:H:X:Y in recording pixels")
    ap.add_argument("--threshold", type=float, default=1.0)
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--grid", type=int, default=24)
    ap.add_argument("--from", dest="from_sec", type=float, default=None)
    ap.add_argument("--to", dest="to_sec", type=float, default=None)
    ap.add_argument("--fps", type=float, default=60.0)
    ap.add_argument("--maps", type=int, default=4, help="how many heat maps to print")
    args = ap.parse_args()

    prev = None
    events = []
    for index, buf, w, h in decode(args.recording, args.crop, args.from_sec, args.to_sec):
        cur = array("B", buf)
        if prev is not None:
            score, cells, box = frame_report(prev, cur, w, h, args.grid)
            if score >= args.threshold:
                events.append((index, score, cells, box, w, h))
        prev = cur

    if not events:
        print("no frame-to-frame change above threshold -- screen was static")
        return

    scale_x = (int(args.crop.split(":")[0]) / w) if args.crop else 1
    scale_y = (int(args.crop.split(":")[1]) / h) if args.crop else 1

    print(f"{len(events)} changed frames (threshold {args.threshold}); "
          f"analysis grid {w}x{h}, window px = analysis px * {scale_x:.1f}")
    print(f"{'frame':>7} {'time':>7} {'score':>7}  changed box in window px (x,y,w,h)")
    for index, score, cells, box, w_, h_ in events[: args.top]:
        minx, miny, maxx, maxy = box
        print(f"{index:7d} {index / args.fps:7.3f} {score:7.2f}  "
              f"({int(minx * scale_x)},{int(miny * scale_y)},"
              f"{int((maxx - minx + 1) * scale_x)},{int((maxy - miny + 1) * scale_y)})")

    for index, score, cells, box, w_, h_ in sorted(events, key=lambda e: -e[1])[: args.maps]:
        print(f"\nheat map, frame {index} (t={index / args.fps:.3f}s, score {score:.2f}):")
        for line in heat_map(cells, args.grid):
            print("  |" + line + "|")


if __name__ == "__main__":
    sys.exit(main())
