#!/usr/bin/env python3
"""probe.py — sample exact pixel colors at given points/regions of a PNG."""
import sys
from PIL import Image

def probe(fn, points):
    im = Image.open(fn).convert("RGB")
    px = im.load()
    W, H = im.size
    print(f"=== {fn} ===")
    for label, x, y in points:
        if 0 <= x < W and 0 <= y < H:
            r, g, b = px[x, y]
            print(f"  {label:28s} ({x:3d},{y:3d}) #{r:02X}{g:02X}{b:02X}")
        else:
            print(f"  {label:28s} ({x:3d},{y:3d}) out of range")

def rowscan(fn, ys, x0=0, x1=320, step=1):
    """print run-length colors of specific rows"""
    im = Image.open(fn).convert("RGB")
    px = im.load()
    print(f"=== rowscan {fn} ===")
    for y in ys:
        runs = []
        cur = px[0, y]; start = 0
        for x in range(1, 320):
            c = px[x, y]
            if c != cur:
                if start != x - 1 or True:
                    runs.append((start, x - 1, cur))
                cur = c; start = x
        runs.append((start, 319, cur))
        # compress: merge runs shorter than 3px into neighbors for readability
        comp = []
        for s, e, c in runs:
            if comp and (e - s < 3 and comp[-1][2] == c or e - s < 2):
                comp[-1] = (comp[-1][0], e, c if e - s >= 2 else comp[-1][2])
            else:
                comp.append((s, e, c))
        print(f" y={y}: " + " | ".join(
            f"{s}-{e}:#{c[0]:02X}{c[1]:02X}{c[2]:02X}" for s, e, c in comp[:14]))

if __name__ == "__main__":
    R = "/tmp/refscreens/"
    # home.png: status bar icons + address bar details
    probe(R + "home.png", [
        ("status-left", 20, 12), ("status-right-batt", 300, 12),
        ("status-mid", 160, 10), ("content", 160, 240),
        ("bar-bg", 160, 490), ("bar-bg2", 5, 520),
        ("tabs-icon", 22, 504), ("reload-icon", 70, 504),
        ("addr-text", 100, 504), ("reader-icon", 246, 504),
        ("more-icon", 295, 504), ("sep1", 48, 504), ("sep2", 268, 504),
    ])
    # scan the address bar center row to find icon x positions
    rowscan(R + "home.png", [504])
    # status bar rows
    rowscan(R + "home.png", [10, 16])
