#!/usr/bin/env python3
"""Bit-exact font-direction and size preview for the real NTOS GDI atlas.

Does not use a host TrueType font: reads payload/ugui_font.h directly.
Uses the same LSB-left and nearest-neighbour pixel rules as coredll.c.
Writes a PNG with Python's stdlib, no Pillow or anti-aliasing required.
"""
import binascii
import re
import struct
import zlib
from pathlib import Path

WIDTH, HEIGHT = 320, 528
FONT_PATH = Path(__file__).resolve().parent.parent / "payload/ugui_font.h"


def font_data():
    text = FONT_PATH.read_text()
    m = re.search(r"nt_font_advance\[95\]=\{([^}]+)\}", text)
    if not m:
        raise ValueError("missing font advances")
    advances = [int(x) for x in m.group(1).split(",")]
    glyphs = {}
    for body, cp in re.findall(
        r"\{((?:0x[0-9a-f]{2},?){14})\}\s*/\* U\+([0-9a-f]+) \*/",
        text, flags=re.I
    ):
        glyphs[int(cp, 16)] = [int(x, 16) for x in re.findall(r"0x[0-9a-f]{2}", body, re.I)]
    assert len(advances) == 95
    assert len(glyphs) == 95 and all(len(g) == 14 for g in glyphs.values())
    return advances, glyphs


def glyph_mask(glyph, height):
    # C code does this with DDA instead of division inside the pixel loop.
    w = (8 * height + 7) // 14
    return [
        [(glyph[(yy * 14) // height] >> min(7, (xx * 14) // height)) & 1
         for xx in range(w)]
        for yy in range(height)
    ]


class Surface:
    def __init__(self):
        self.pixels = bytearray(WIDTH * HEIGHT * 3)

    def rect(self, x0, y0, x1, y1, color):
        x0, y0 = max(0, x0), max(0, y0)
        x1, y1 = min(WIDTH, x1), min(HEIGHT, y1)
        if x1 <= x0 or y1 <= y0:
            return
        value = bytes(color)
        for y in range(y0, y1):
            i = (y * WIDTH + x0) * 3
            self.pixels[i:i + (x1 - x0) * 3] = value * (x1 - x0)

    def text(self, x, y, text, color, height, advances, glyphs):
        for ch in text:
            cp = ord(ch)
            if cp < 32 or cp > 126:
                cp = ord("?")
            mask = glyph_mask(glyphs[cp], height)
            for yy, row in enumerate(mask):
                for xx, bit in enumerate(row):
                    if bit:
                        self.rect(x + xx, y + yy, x + xx + 1, y + yy + 1, color)
            x += (advances[cp - 32] * height + 7) // 14

    def write_png(self, dest):
        raw = b"".join(
            b"\x00" + self.pixels[y * WIDTH * 3:(y + 1) * WIDTH * 3]
            for y in range(HEIGHT)
        )
        def chunk(name, data):
            return (struct.pack(">I", len(data)) + name + data +
                    struct.pack(">I", binascii.crc32(name + data) & 0xffffffff))
        image = (b"\x89PNG\r\n\x1a\n" +
                 chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)) +
                 chunk(b"IDAT", zlib.compress(raw, 9)) +
                 chunk(b"IEND", b""))
        Path(dest).write_bytes(image)


def render_storage(dest, adv, glyphs):
    s = Surface()
    white, dim, blue = (242, 242, 242), (155, 155, 155), (51, 103, 241)
    s.rect(0, 0, 320, 528, (0, 0, 0))
    s.rect(0, 0, 320, 50, (27, 27, 27))
    s.text(51, 17, "This Device", white, 14, adv, glyphs)
    s.rect(13, 18, 33, 19, white)
    s.rect(13, 25, 33, 26, white)
    s.rect(13, 32, 33, 33, white)
    s.text(14, 82, "Storage usage", white, 22, adv, glyphs)
    s.text(14, 111, "Last refreshed: demo", blue, 14, adv, glyphs)
    s.text(14, 147, "Total: 29.1 GB (example)", white, 14, adv, glyphs)
    s.rect(14, 178, 304, 189, (54, 54, 54))
    s.rect(14, 178, 301, 189, blue)
    s.text(14, 198, "Used: 28.9 GB", white, 14, adv, glyphs)
    groups = ["Apps & games", "System & reserved", "Pictures", "Music", "Maps"]
    sizes = ["16.3 GB", "4.43 GB", "4.26 GB", "3.06 GB", "406 MB"]
    amounts = [57, 15, 11, 8, 2]
    for i, (label, size, amount) in enumerate(zip(groups, sizes, amounts)):
        y = 235 + i * 46
        s.rect(14, y + 2, 25, y + 13, blue)
        s.text(34, y, label, white, 14, adv, glyphs)
        s.text(238, y, size, dim, 14, adv, glyphs)
        s.rect(34, y + 23, 303, y + 30, (54, 54, 54))
        s.rect(34, y + 23, 34 + 269 * amount // 100, y + 30, blue)
    s.rect(145, 499, 152, 506, white)
    s.rect(154, 499, 161, 506, white)
    s.rect(145, 508, 152, 516, white)
    s.rect(154, 508, 161, 516, white)
    s.write_png(dest)


if __name__ == "__main__":
    advances, glyphs = font_data()
    # Distinguish F from its mirrored form. LSB-left bit 1 is the stem.
    assert glyphs[ord("F")][3] & 0x02
    assert not glyphs[ord("F")][3] & 0x80
    assert glyph_mask(glyphs[ord("F")], 14)[3][1] == 1
    assert len(glyph_mask(glyphs[ord("F")], 22)) == 22
    render_storage("dist/native_gdi_storage_preview.png", advances, glyphs)
    print("PASS native GDI glyph direction, 14px/22px scaling, 320x528 PNG")
