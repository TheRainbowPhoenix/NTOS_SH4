#!/usr/bin/env python3
"""vec_icons.py — multi-layer coloured VEC2 (SVF) icon set for the File
Explorer port.

The W10M type icons are layered colour illustrations (Folder_Large is a
two-tone manila folder with a lighter paper sheet behind the front panel;
the image/video/audio/doc type glyphs each carry brand colours), not the
monochrome MDL2 strokes used for command affordances. This generator
authors them as VEC2 command streams (same binary format + decoder as the
phone system icons, payload/phone/svf.c) with one SETSTYLE per layer, so
each icon is a stack of coloured polygon/circle/rect layers.

Output: files/app/files_vecicons.h (C byte arrays + VI_* ids). The
generated streams are validated against the exact grammar svf.c's scan()
accepts, so a bad icon fails the build instead of the device.
"""
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / '..' / 'app' / 'files_vecicons.h'


class Vec:
    """VEC2 command stream builder (mirrors payload/phone/generate_apps.py)."""

    def __init__(self, vw=32, vh=32):
        self.b = bytearray(b'VEC2' + b'\x60' + struct.pack('>hh', vw, vh))
        self.vw, self.vh = vw, vh

    def style(self, fill=0xffff, stroke=0xffff):
        self.b += b'\x01' + struct.pack('>HH', fill, stroke)
        return self

    def line(self, a, b_, c, d):
        self.b += b'\x30' + struct.pack('>hhhh', a, b_, c, d)
        return self

    def stroke(self, a, b_, c, d, width=2):
        self.b += b'\x31' + struct.pack('>hhhhB', a, b_, c, d, width)
        return self

    def arc(self, cx, cy, r, start, count, width=2):
        self.b += b'\x22' + struct.pack('>hhhBBB', cx, cy, r, start, count, width)
        return self

    def rect(self, a, b_, c, d):
        self.b += b'\x10' + struct.pack('>hhhh', a, b_, c, d)
        return self

    def circle(self, x, y, r):
        self.b += b'\x20' + struct.pack('>hhh', x, y, r)
        return self

    def poly(self, *p):
        self.b += bytes((0x40, len(p) // 2)) + struct.pack('>' + 'h' * len(p), *p)
        return self

    def end(self):
        return bytes(self.b + b'\0')


def rgb(r, g, b):
    """RGB888 -> RGB555 word (BE) used by SETSTYLE."""
    return (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)


# palette (sampled from the W10M reference captures + MDL file-type colors)
FOL_BACK = rgb(0xE0, 0xA6, 0x3C)   # back panel, darker manila
FOL_PAPER = rgb(0xFC, 0xE9, 0xA0)  # paper sheet peeking behind the front
FOL_FRONT = rgb(0xF5, 0xC2, 0x42)  # front panel (reference Folder_Large)
FOL_HI = rgb(0xFF, 0xDA, 0x7A)     # top edge highlight
FOL_SHADE = rgb(0xE8, 0xB3, 0x39)  # bottom shade
PAGE = rgb(0xFF, 0xFF, 0xFF)
PAGE_EDGE = rgb(0x9A, 0x9A, 0x9A)
PAGE_FOLD = rgb(0xD0, 0xD0, 0xD0)
INK = rgb(0x33, 0x33, 0x33)
BLUE = rgb(0x00, 0x78, 0xD7)
BLUE_D = rgb(0x00, 0x52, 0xB0)
SKY = rgb(0x9F, 0xD3, 0xF0)
SUN = rgb(0xFF, 0xD3, 0x4D)
MTN_A = rgb(0x3F, 0x8F, 0x4F)
MTN_B = rgb(0x2F, 0x6F, 0x3F)
NOTE = rgb(0x6B, 0x57, 0xD9)       # audio accent (Groove purple)
FILM = rgb(0x2B, 0x57, 0x9A)       # video accent (Movies & TV blue)
FILM_D = rgb(0x1E, 0x3F, 0x73)
GREEN = rgb(0x00, 0x83, 0x50)
SLATE = rgb(0x3A, 0x3A, 0x3A)
SCREEN = rgb(0xBF, 0xE4, 0xFA)
GOLD = rgb(0xC8, 0xA0, 0x3C)


def icon_folder():
    """Manila folder: back panel + paper sheet + front (tab+body) + highlights.

    Layer order mirrors the reference Folder_Large: darker open back, a
    lighter paper sheet rising behind the front panel, the amber front
    with its tab, then a top highlight and bottom shade for depth.
    """
    v = Vec()
    # back panel (open folder rear, offset up-right of the front)
    v.style(FOL_BACK)
    v.poly(3, 5, 24, 5, 30, 11, 30, 27, 3, 27)
    # paper sheet inside (peeks above the front panel, slightly rotated)
    v.style(FOL_PAPER)
    v.poly(6, 8, 25, 8, 27, 11, 27, 12, 8, 12)
    # front panel: tab then body, one polygon
    v.style(FOL_FRONT)
    v.poly(1, 8, 11, 8, 14, 12, 31, 12, 31, 28, 1, 28)
    # top edge highlight strip
    v.style(FOL_HI)
    v.rect(1, 12, 31, 14)
    # bottom shade
    v.style(FOL_SHADE)
    v.rect(1, 26, 31, 28)
    return v.end()


def icon_file():
    """Generic file page: white sheet, folded corner, grey rule lines.

    The page shape is delimited by 1px PAGE_EDGE strokes on the left,
    right and bottom (plus the top run up to the dog-ear) — on the white
    list background the border is what makes the sheet read as a sheet.
    NOTE: v.style(stroke=...) — the STROKE colour drives v.stroke(); a
    positional v.style(PAGE_EDGE) would set the fill and silently draw
    nothing (svf.c skips strokes when the style stroke is 0xffff).
    """
    v = Vec()
    v.style(PAGE)
    v.poly(6, 1, 21, 1, 26, 6, 26, 31, 6, 31)
    v.style(PAGE_FOLD)
    v.poly(21, 1, 26, 6, 21, 6)
    v.style(stroke=PAGE_EDGE)
    v.stroke(6, 1, 21, 1, 1)      # top edge up to the fold
    v.stroke(26, 6, 26, 31, 1)    # right
    v.stroke(6, 31, 26, 31, 1)    # bottom
    v.stroke(6, 1, 6, 31, 1)      # left
    v.style(stroke=PAGE_EDGE)
    v.stroke(10, 12, 22, 12, 1)
    v.stroke(10, 17, 22, 17, 1)
    v.stroke(10, 22, 18, 22, 1)
    return v.end()


def icon_doc():
    """Document: white page + folded corner + blue text rules + blue title."""
    v = Vec()
    v.style(PAGE)
    v.poly(6, 1, 21, 1, 26, 6, 26, 31, 6, 31)
    v.style(PAGE_FOLD)
    v.poly(21, 1, 26, 6, 21, 6)
    v.style(stroke=PAGE_EDGE)
    v.stroke(6, 1, 21, 1, 1)
    v.stroke(26, 6, 26, 31, 1)
    v.stroke(6, 31, 26, 31, 1)
    v.stroke(6, 1, 6, 31, 1)
    v.style(BLUE)
    v.stroke(10, 11, 22, 11, 2)   # title
    v.style(BLUE_D)
    v.stroke(10, 16, 22, 16, 1)   # rules
    v.stroke(10, 20, 22, 20, 1)
    v.stroke(10, 24, 22, 24, 1)
    return v.end()


def icon_image():
    """Photo: white frame, sky, sun, two mountain ridges (camera roll)."""
    v = Vec()
    v.style(PAGE)
    v.rect(1, 4, 31, 29)
    v.style(SKY)
    v.rect(2, 5, 30, 18)
    v.style(SUN)
    v.circle(24, 10, 3)
    v.style(MTN_A)
    v.poly(2, 18, 10, 8, 17, 18)
    v.style(MTN_B)
    v.poly(12, 18, 20, 10, 30, 18)
    v.style(GREEN)
    v.rect(2, 18, 30, 28)
    v.style(stroke=PAGE_EDGE)
    v.stroke(1, 4, 31, 4, 1)
    v.stroke(31, 4, 31, 29, 1)
    v.stroke(1, 29, 31, 29, 1)
    v.stroke(1, 4, 1, 29, 1)
    return v.end()


def icon_audio():
    """Music note: Groove-purple double note with stems and flags."""
    v = Vec()
    v.style(NOTE)
    v.rect(12, 3, 15, 21)          # stem L
    v.rect(24, 1, 27, 17)          # stem R
    v.poly(12, 3, 27, 1, 27, 6, 12, 8)   # beam
    v.circle(8, 23, 4)             # head L
    v.circle(20, 21, 4)            # head R
    return v.end()


def icon_video():
    """Film: Movies&TV blue strip with sprocket holes + lighter screen."""
    v = Vec()
    v.style(FILM)
    v.rect(1, 6, 31, 26)
    v.style(SCREEN)
    v.rect(6, 10, 26, 22)
    v.style(PAGE)
    for y in (8, 13, 18, 23):
        v.rect(2, y, 5, y + 2)     # left sprockets
        v.rect(27, y, 30, y + 2)   # right sprockets
    v.style(FILM_D)
    v.rect(1, 6, 31, 8)
    v.rect(1, 24, 31, 26)
    return v.end()


def icon_html():
    """HTML document: white page + Edge-blue globe with green meridian."""
    v = Vec()
    v.style(PAGE)
    v.poly(6, 1, 21, 1, 26, 6, 26, 31, 6, 31)
    v.style(PAGE_FOLD)
    v.poly(21, 1, 26, 6, 21, 6)
    v.style(stroke=PAGE_EDGE)
    v.stroke(6, 1, 21, 1, 1)
    v.stroke(26, 6, 26, 31, 1)
    v.stroke(6, 31, 26, 31, 1)
    v.stroke(6, 1, 6, 31, 1)
    # globe
    v.style(0xffff, BLUE)
    v.arc(16, 18, 6, 0, 32, 1)     # equator ring
    v.style(0xffff, GREEN)
    v.arc(16, 18, 6, 8, 16, 1)     # meridian (vertical)
    v.style(BLUE)
    v.circle(16, 18, 3)
    return v.end()


def icon_device():
    """This device: slate phone body, light screen, speaker + home dot."""
    v = Vec()
    v.style(SLATE)
    v.rect(9, 1, 23, 31)
    v.style(SCREEN)
    v.rect(11, 6, 21, 25)
    v.style(INK)
    v.rect(14, 3, 18, 4)           # earpiece
    v.circle(16, 28, 1)            # home
    return v.end()


def icon_sdcard():
    """SD card: grey-green body, angled corner, gold contacts."""
    v = Vec()
    v.style(SLATE)
    v.poly(8, 6, 13, 1, 24, 1, 24, 31, 8, 31)
    v.style(GOLD)
    for i in range(4):
        v.rect(10 + i * 4, 27, 12 + i * 4, 31)
    v.style(INK)
    for i in range(3):
        v.rect(11 + i * 4, 3, 12 + i * 4, 7)  # write-protect notches
    return v.end()


ICONS = [
    ('folder', icon_folder),
    ('file', icon_file),
    ('doc', icon_doc),
    ('image', icon_image),
    ('audio', icon_audio),
    ('video', icon_video),
    ('html', icon_html),
    ('device', icon_device),
    ('sdcard', icon_sdcard),
]


def validate(data):
    """Byte-exact port of svf.c scan(): every stream must pass before it
    can ship to the device."""
    b, n = data, len(data)
    if not b or n < 5 or n > 4096 or b[:3] != b'VEC' or b[3] not in (ord('1'), ord('2')):
        return 'magic/size'
    p, commands = 4, 0
    while p < n and commands <= 512:
        op = b[p]
        p += 1
        commands += 1
        if op == 0:
            return None if p == n else 'trailing bytes after end'
        if op in (1, 0x60):
            need = 4
        elif op in (0x10, 0x21, 0x30):
            need = 8
        elif op in (0x11, 0x22, 0x31):
            need = 9
        elif op == 0x20:
            need = 6
        elif op == 0x40:
            if p >= n or b[p] < 3:
                return 'poly count'
            need = 1 + 4 * b[p]
        elif op == 0x55:
            if p >= n:
                return 'skip len'
            need = 1 + b[p]
        elif op == 0x50:
            if n - p < 5:
                return 'text hdr'
            need = 5 + b[p + 4]
        else:
            return f'op {op:#x}'
        if need > n - p:
            return f'op {op:#x} truncated'
        if op in (0x22, 0x31) and (b[p + 8] == 0 or b[p + 8] > 8):
            return 'stroke width'
        if op == 0x22 and (b[p + 6] >= 32 or b[p + 7] > 32):
            return 'arc range'
        p += need
    return 'too many commands / no end'


def main():
    lines = [
        '/* files_vecicons.h — generated by files/tools/vec_icons.py.',
        ' * Multi-layer colour VEC2 (SVF) type icons for the W10M File',
         ' * Explorer port: every icon is a stack of coloured layers decoded',
        ' * by the shared payload/phone/svf.c engine at paint time.',
        ' * DO NOT EDIT BY HAND — regenerate with the tool. */',
        '#ifndef FILES_VECICONS_H',
        '#define FILES_VECICONS_H',
        '',
    ]
    ids = []
    blobs = []
    for i, (name, fn) in enumerate(ICONS):
        data = fn()
        err = validate(data)
        if err:
            print(f'{name}: INVALID VEC stream ({err})', file=sys.stderr)
            return 1
        ids.append(f'#define VI_{name.upper()} {i}')
        blobs.append(
            f'static const unsigned char vi{i}[] = {{'
            + ','.join(str(x) for x in data) + '};')
    lines += ids
    lines.append('')
    lines += blobs
    lines.append('')
    lines.append('typedef struct { const unsigned char *data; unsigned length; } VecIcon;')
    lines.append('static const VecIcon vec_icons[] = {'
                 + ','.join('{vi%d,sizeof(vi%d)}' % (i, i) for i in range(len(ICONS)))
                 + '};')
    lines.append('#endif')
    OUT.write_text('\n'.join(lines) + '\n')
    total = sum(len(fn()) for _, fn in ICONS)
    print(f'wrote {OUT} ({len(ICONS)} icons, {total} bytes of VEC2 data)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
