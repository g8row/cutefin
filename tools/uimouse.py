#!/usr/bin/env python3
"""Synthetic pointer/scroll driver via CoreGraphics (ctypes, no pyobjc).

usage:
  mouse.py move X Y
  mouse.py glide X1 Y1 X2 Y2 [steps] [delay]
  mouse.py click X Y
  mouse.py scroll LINES [count] [delay]      # negative LINES = content down
  mouse.py wheelburst X Y LINES COUNT DELAY  # move there, then scroll repeatedly
"""
import ctypes, ctypes.util, sys, time

cf = ctypes.CDLL(ctypes.util.find_library("CoreFoundation"))
cg = ctypes.CDLL(ctypes.util.find_library("ApplicationServices"))


class CGPoint(ctypes.Structure):
    _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double)]


cg.CGEventCreateMouseEvent.restype = ctypes.c_void_p
cg.CGEventCreateMouseEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint32, CGPoint, ctypes.c_uint32]
cg.CGEventCreateScrollWheelEvent.restype = ctypes.c_void_p
cg.CGEventCreateScrollWheelEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32,
                                             ctypes.c_int32, ctypes.c_int32]
cg.CGEventPost.argtypes = [ctypes.c_uint32, ctypes.c_void_p]
cf.CFRelease.argtypes = [ctypes.c_void_p]

kMouseMoved, kLeftDown, kLeftUp = 5, 1, 2
kHIDEventTap = 0
kScrollLine = 1


def post(ev):
    cg.CGEventPost(kHIDEventTap, ev)
    cf.CFRelease(ev)


def move(x, y):
    post(cg.CGEventCreateMouseEvent(None, kMouseMoved, CGPoint(x, y), 0))


def click(x, y):
    move(x, y)
    time.sleep(0.05)
    post(cg.CGEventCreateMouseEvent(None, kLeftDown, CGPoint(x, y), 0))
    time.sleep(0.05)
    post(cg.CGEventCreateMouseEvent(None, kLeftUp, CGPoint(x, y), 0))


def scroll(lines):
    post(cg.CGEventCreateScrollWheelEvent(None, kScrollLine, 1, int(lines), 0))


def main():
    a = sys.argv[1:]
    cmd = a[0]
    if cmd == "move":
        move(float(a[1]), float(a[2]))
    elif cmd == "click":
        click(float(a[1]), float(a[2]))
    elif cmd == "glide":
        x1, y1, x2, y2 = map(float, a[1:5])
        steps = int(a[5]) if len(a) > 5 else 30
        delay = float(a[6]) if len(a) > 6 else 0.012
        for i in range(steps + 1):
            t = i / steps
            move(x1 + (x2 - x1) * t, y1 + (y2 - y1) * t)
            time.sleep(delay)
    elif cmd == "scroll":
        lines = int(a[1])
        count = int(a[2]) if len(a) > 2 else 1
        delay = float(a[3]) if len(a) > 3 else 0.05
        for _ in range(count):
            scroll(lines)
            time.sleep(delay)
    elif cmd == "wheelburst":
        x, y, lines, count, delay = float(a[1]), float(a[2]), int(a[3]), int(a[4]), float(a[5])
        move(x, y)
        time.sleep(0.2)
        for _ in range(count):
            scroll(lines)
            time.sleep(delay)
    else:
        raise SystemExit(__doc__)


main()
