#!/usr/bin/env python3
"""Pixel-level analysis of Edge reference screenshots for UI reimplementation."""
import sys, os
from PIL import Image

def hexc(c):
    r, g, b = c[0], c[1], c[2]
    return f"#{r:02X}{g:02X}{b:02X}"

def analyze(fn):
    im = Image.open(fn).convert("RGB")
    W, H = im.size
    px = im.load()
    print(f"=== {os.path.basename(fn)} ({W}x{H}) ===")

    # 1. Row-band analysis: average color per row, detect distinct horizontal bands
    rows = []
    for y in range(H):
        rs = gs = bs = 0
        for x in range(0, W, 4):
            r, g, b = px[x, y]
            rs += r; gs += g; bs += b
        n = len(range(0, W, 4))
        rows.append((rs//n, gs//n, bs//n))
    # compress to bands where color changes < 8
    bands = []
    cur = rows[0]; start = 0
    for y in range(1, H):
        r, g, b = rows[y]
        if abs(r-cur[0])+abs(g-cur[1])+abs(b-cur[2]) > 24:
            bands.append((start, y-1, cur))
            cur = rows[y]; start = y
    bands.append((start, H-1, cur))
    # merge tiny bands
    merged = []
    for b in bands:
        if merged and b[1]-b[0] < 3 and abs(b[2][0]-merged[-1][2][0])+abs(b[2][1]-merged[-1][2][1])+abs(b[2][2]-merged[-1][2][2]) < 30:
            merged[-1] = (merged[-1][0], b[1], merged[-1][2])
        else:
            merged.append(list(b))
    print("Row bands (y0-y1 avg-color):")
    for y0, y1, c in merged:
        if y1 - y0 >= 2:
            print(f"  {y0:3d}-{y1:3d} h={y1-y0+1:3d}  rgb{tuple(c)} {hexc(c)}")

    # 2. Vertical line detection in bottom bar (separators): scan rows 480-520
    print("Bottom-bar vertical structure (rows 470-527), column darkness pattern:")
    ytop, ybot = 470, H-1
    colscore = []
    for x in range(W):
        d = 0
        for y in range(ytop, ybot+1, 2):
            r, g, b = px[x, y]
            d += r+g+b
        colscore.append(d)
    # find local minima columns (dark vertical lines) - print darkest cols
    dark_cols = sorted(range(W), key=lambda x: colscore[x])[:12]
    print(f"  darkest columns: {sorted(dark_cols)}")

    # 3. Sample specific points
    samples = [(160, 5), (160, 30), (160, 260), (160, 500), (160, 520), (10, 495), (310, 495)]
    print("Point samples:")
    for x, y in samples:
        print(f"  ({x},{y}) {hexc(px[x,y])}")
    print()

if __name__ == "__main__":
    for fn in sys.argv[1:]:
        analyze(fn)
