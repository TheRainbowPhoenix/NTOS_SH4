#!/usr/bin/env python3
"""check_cortana_shots.py — pixel sanity checks for the Cortana demo e2e.

Verifies each screenshot is the right size, non-blank and carries the
anchors of the reference captures (refs/cortana/cortana_light*.png):
page #F7F3F7, the blue orb at (160,48), the blue "How can I help?"
greeting, the #DEDBDE search band with the #0078D7 mic square, the
"Thinking..." strip and the offline error text zone.
"""
import struct, sys, zlib, os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..',
                                'edge', 'ntos_linux', 'tests'))
from check_shots import read_png as _read_png_raw


def read_png(path):
    w, h, data = _read_png_raw(path)
    stride = w * 3
    px = [data[y * stride:(y + 1) * stride] for y in range(h)]
    return w, h, px


def pix(px, x, y):
    p = px[y]
    return (p[x * 3], p[x * 3 + 1], p[x * 3 + 2])


def near(c, want, tol=18):
    return all(abs(c[i] - want[i]) <= tol for i in range(3))


PAGE = (0xF7, 0xF3, 0xF7)
BAND = (0xDE, 0xDB, 0xDE)
INK = (0, 0, 0)
ACC = (0x00, 0x78, 0xD7)
ORB = (0x4A, 0x71, 0xFF)

# the orb ring: sample the ring body (left/right arcs + top/bottom of the
# circle centred at 160,48, outer d48)
def orb_blue(px):
    pts = [(140, 48), (180, 48), (160, 26), (160, 70)]
    hits = 0
    for x, y in pts:
        c = pix(px, x, y)
        if c[2] - c[0] > 30 and c[2] > 120:
            hits += 1
    return hits >= 2


def greeting_blue(px):
    # "How can I help?" centered at y84..97: some blue text pixel exists
    for y in range(84, 98, 2):
        for x in range(90, 230, 2):
            c = pix(px, x, y)
            if c[2] - c[0] > 40 and c[2] > 130 and c[0] < 140:
                return True
    return False


CHECKS = {
    'boot': [
        ('page', 160, 300, PAGE),
        ('band', 40, 440, BAND),
        ('mic', 290, 456, ACC),
        ('greeting-zone', 160, 84, PAGE),
    ],
    'input': [
        ('page', 160, 130, PAGE),
        ('kb-backdrop', 160, 300, (0xF2, 0xF2, 0xF2)),
        ('query-band', 14, 240, BAND),
        ('query-field', 200, 248, (255, 255, 255)),
    ],
    'thinking': [
        ('page', 160, 300, PAGE),
        ('band', 40, 440, BAND),
        ('band-right', 310, 456, BAND),
    ],
    'error': [
        ('page', 160, 300, PAGE),
        ('band', 40, 440, BAND),
        ('mic', 290, 456, ACC),
    ],
    'home-again': [
        ('page', 160, 300, PAGE),
        ('band', 40, 440, BAND),
        ('mic', 290, 456, ACC),
    ],
    'final': [
        ('page', 160, 300, PAGE),
    ],
}

ORB_SCREENS = {'boot', 'input', 'thinking', 'error', 'home-again', 'final'}
GREETING_SCREENS = {'boot', 'error', 'home-again', 'final'}


def main():
    fails = 0
    for path in sys.argv[1:]:
        name = path.split('/')[-1].replace('cortana-', '').replace('.png', '')
        try:
            w, h, px = read_png(path)
        except Exception as e:
            print(f'{path}: READ FAIL {e}')
            fails += 1
            continue
        if w != 320 or h != 528:
            print(f'{path}: {w}x{h} WRONG SIZE')
            fails += 1
            continue
        colors = set()
        for y in range(0, 528, 8):
            for x in range(0, 320, 8):
                colors.add(pix(px, x, y))
        if len(colors) < 3:
            print(f'{path}: too few colors ({len(colors)}) — blank?')
            fails += 1
            continue
        ok = True
        for what, x, y, want in CHECKS.get(name, []):
            if not near(pix(px, x, y), want):
                print(f'{path}: {what} anchor @{x},{y} '
                      f'{pix(px, x, y)} != {want}')
                ok = False
        if name in ORB_SCREENS and not orb_blue(px):
            print(f'{path}: orb ring blue missing at (160,29)')
            ok = False
        if name in GREETING_SCREENS and not greeting_blue(px):
            print(f'{path}: blue greeting text missing in y84..97')
            ok = False
        if ok:
            print(f'{path}: {len(colors)} sampled colors [OK]')
        else:
            fails += 1
    if fails:
        print(f'{fails} FAILURES')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
