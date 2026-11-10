#!/usr/bin/env python3
"""bake_calc_fonts.py — append 32 px + 44 px ASCII bitmap fonts to the
shared payload/phone/font_bitmap.h (needed for the WindowsCalculator port:
the 46 px result display and the 32-ish converter values; the existing
table tops out at 22 px for full ASCII, and no existing app requests
anything above 22 px, so pick_font() results are unchanged for them).

Same glyph model as payload/phone/bake_font.py (PhoneGlyph{offset,width,
advance} + 1-bpp row bits), baked from Liberation Sans (Segoe UI stand-in
available on this host).
"""
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SRC = Path(__file__).resolve().parents[2] / "payload" / "phone" / "font_bitmap.h"
FACE = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
SIZES = (32, 44)
FIRST, LAST = 32, 126  # printable ASCII


def bake(h):
    font = ImageFont.truetype(FACE, h)
    data = []
    glyphs = []
    for cp in range(FIRST, LAST + 1):
        ch = chr(cp)
        advance = max(1, round(font.getlength(ch)))
        width = min(64, advance + 1)
        image = Image.new("L", (width, h))
        d = ImageDraw.Draw(image)
        d.text((0, h - max(2, h // 9)), ch, font=font, fill=255, anchor="ls")
        glyphs.append((len(data), width, advance))
        stride = (width + 7) // 8
        for y in range(h):
            for byte in range(stride):
                data.append(sum(
                    1 << bit for bit in range(8)
                    if byte * 8 + bit < width and
                    image.getpixel((byte * 8 + bit, y)) >= 112))
    tag = f"calc{h}"
    return (f"static const PhoneGlyph pg{tag}[]={{{','.join('{%d,%d,%d}' % g for g in glyphs)}}};\n"
            f"static const unsigned char pb{tag}[]={{{','.join(str(n) for n in data)}}};\n"), tag


def main():
    text = SRC.read_text()
    if "pgcalc44" in text:
        print("calc fonts already present")
        return
    parts = [text.rstrip()]
    tags = []
    for h in SIZES:
        decl, tag = bake(h)
        parts.append(decl)
        tags.append((h, tag))
    # extend the font table (last statement in the file, i.e. parts[0])
    entries = ",".join("{%d,%d,%d,pg%s,pb%s}" % (h, FIRST, LAST - FIRST + 1, t, t)
                       for h, t in tags)
    m = re.search(r"static const PhoneFont phone_fonts\[\]=\{(.*)\};\s*$",
                  parts[0], re.S)
    assert m, "phone_fonts table not found"
    table = m.group(1).rstrip()
    parts[0] = parts[0][:m.start(1)] + table + "," + entries + parts[0][m.end(1):]
    SRC.write_text("\n".join(parts) + "\n")
    print(f"appended {len(SIZES)} fonts to {SRC}")


if __name__ == "__main__":
    main()
