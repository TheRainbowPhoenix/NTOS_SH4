#!/usr/bin/env python3
"""navbar_audit.py — which CI screenshots show the shell nav bar?

The shell nav bar (shellchrome.dll) owns y480..528 on EVERY hosted app
screen (device parity: the bar never goes away, SIP or not). This script
profiles the bottom 48 rows of each screenshot and classifies them:
  NAVBAR   — the strip matches the shellchrome signature (3 glyphs,
             light/dark theme bg, no foreign content bleeding into it)
  KEYBOARD — the shared SIP paints y271..480; the strip below must still
             be NAVBAR (else it's BAR+KBD-CLIP = the 8px-gap class of bug)
  OTHER    — something else owns the strip (app bar, content, black gap)

Usage: navbar_audit.py <dir-with-pngs>
"""
import sys
import zlib
import struct
from pathlib import Path


def read_png(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos = 8
    w = h = None
    idat = b""
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h = struct.unpack(">II", chunk[:8])
            bitd, color = chunk[8], chunk[9]
            assert bitd == 8 and color == 2, (path, bitd, color)
        elif typ == b"IDAT":
            idat += chunk
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * 3
    fb = []
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        p += 1
        row = bytearray(raw[p:p + stride])
        p += stride
        if f == 0:
            pass
        elif f == 1:
            for i in range(3, stride):
                row[i] = (row[i] + row[i - 3]) & 0xFF
        elif f == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                left = row[i - 3] if i >= 3 else 0
                row[i] = (row[i] + (left + prev[i]) // 2) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = row[i - 3] if i >= 3 else 0
                b = prev[i]
                c = prev[i - 3] if i >= 3 else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                row[i] = (row[i] + pr) & 0xFF
        fb.append(bytes(row))
        prev = row
    return w, h, fb


def px(fb, w, x, y):
    r = fb[y][x * 3]
    g = fb[y][x * 3 + 1]
    b = fb[y][x * 3 + 2]
    return r, g, b


def row_stats(fb, w, y):
    """mean colour + non-uniformity of a row"""
    rs = gs = bs = 0
    for x in range(w):
        r, g, b = px(fb, w, x, y)
        rs += r
        gs += g
        bs += b
    n = w
    mean = (rs // n, gs // n, bs // n)
    var = sum(abs(px(fb, w, x, y)[0] - mean[0]) for x in range(w)) / n
    return mean, var


def main():
    d = Path(sys.argv[1])
    files = sorted(d.glob("edge-*.png"))
    print(f"{'shot':34s} {'strip y480..528':16s} verdict")
    for f in files:
        w, h, fb = read_png(f)
        # strip signature: mean/var of the 8 rows 480..487 and 520..527
        m0, v0 = row_stats(fb, w, 480)
        m1, v1 = row_stats(fb, w, 527)
        m2, v2 = row_stats(fb, w, 504)
        # dark ink rows carry glyph content (var>8)
        glyphs = v0 > 6 or v2 > 6
        flat_dark = (m0[0] < 60 and m1[0] < 60)
        flat_light = (m0[0] > 200 and m1[0] > 200)
        if flat_light and not glyphs:
            kind = "NAVBAR-light?"
        elif flat_dark and not glyphs:
            kind = "BLACK-GAP!!"
        elif glyphs:
            kind = "CONTENT/ICONS"
        else:
            kind = f"mix m480={m0} m527={m1}"
        print(f"{f.name:34s} y480={m0} v{v0:5.1f} y504={m2} v{v2:5.1f}  {kind}")


if __name__ == "__main__":
    main()
