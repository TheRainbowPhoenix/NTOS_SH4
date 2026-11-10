#!/usr/bin/env python3
"""stripdump.py — ASCII render of the bottom 48px strip (the nav-bar zone).

Downsamples y480..528 to 80x12 blocks (4x4 px) using luminance chars so the
nav-bar geometry of a reference capture and a CI shot can be compared by eye
in a terminal. Usage: stripdump.py <png> [png2 ...]
"""
import struct
import sys
import zlib
from pathlib import Path

CHARS = " .:-=+*#%@"


def read_png(path):
    data = Path(path).read_bytes()
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
            if color == 6:
                ch = 4
            elif color == 2:
                ch = 3
            elif color == 0:
                ch = 1
            else:
                raise SystemExit(f"{path}: color type {color} unsupported")
        elif typ == b"IDAT":
            idat += chunk
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * ch
    fb = []
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        p += 1
        row = bytearray(raw[p:p + stride])
        p += stride
        if f == 1:
            for i in range(ch, stride):
                row[i] = (row[i] + row[i - ch]) & 0xFF
        elif f == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                left = row[i - ch] if i >= ch else 0
                row[i] = (row[i] + (left + prev[i]) // 2) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = row[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                row[i] = (row[i] + pr) & 0xFF
        fb.append(bytes(row))
        prev = row
    return w, h, fb, ch


def lum(fb, ch, x, y):
    i = x * ch
    if ch >= 3:
        r, g, b = fb[y][i], fb[y][i + 1], fb[y][i + 2]
        return (r * 299 + g * 587 + b * 114) // 1000
    return fb[y][i]


def main():
    for path in sys.argv[1:]:
        w, h, fb, ch = read_png(path)
        print(f"== {path} (y480..528, 4x4 blocks; nav glyphs show as ink) ==")
        for by in range(12):
            y = 480 + by * 4
            line = []
            for bx in range(80):
                x = bx * 4
                # max-luminance over the 4x4 block (ink = bright on dark,
                # dark on light; use deviation from the strip's dominant tone)
                vals = [lum(fb, ch, x + dx, y + dy) for dx in range(4) for dy in range(4)]
                lo, hi = min(vals), max(vals)
                span = hi - lo
                c = CHARS[min(9, span // 26)]
                line.append(c)
            print("".join(line))
        print()


if __name__ == "__main__":
    main()
