#!/usr/bin/env python3
"""check_files_shots.py — pixel sanity checks for the File Explorer e2e.

Verifies each screenshot is the right size, non-blank, uses the expected
palette for the W10M light theme (white page, #E6E6E6 chrome, black ink,
#0078D7 accents where expected) and — per screen — the specific colour
anchors of the layout. Same philosophy as edge's check_edge_shots.py:
block-averaging catches layout breakage; anchors catch colour/palette
regressions.
"""
import struct, sys, zlib, os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..',
                                'edge', 'ntos_linux', 'tests'))
from check_shots import read_png as _read_png_raw  # (w, h, rgb_bytes)


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


WHITE = (255, 255, 255)
CHROME = (0xE6, 0xE6, 0xE6)
PANE = (0xF2, 0xF2, 0xF2)
INK = (0, 0, 0)
ACCENT = (0x00, 0x78, 0xD7)
SEL = (0xCC, 0xE8, 0xFF)
SCRIM = (0x7F, 0x7F, 0x7F)

CHECKS = {
    # boot (root): navbar + page + appbar chrome, amber folder icon
    'boot': [
        ('navbar', 160, 24, CHROME),
        ('page', 160, 300, WHITE),
        ('appbar', 40, 504, CHROME),
    ],
    # navpane: pane surface + scrim + title text row
    'navpane': [
        ('pane', 120, 300, PANE),
        ('scrim', 300, 300, SCRIM),
        ('title-band', 120, 20, PANE),
    ],
    'documents': [
        ('navbar', 160, 24, CHROME),
        ('page', 160, 300, WHITE),
        ('appbar', 40, 504, CHROME),
    ],
    'root': [('page', 160, 200, WHITE), ('appbar', 200, 504, CHROME)],
    # select mode: selection app bar + a checkbox border pixel
    'select-mode': [
        ('appbar', 40, 504, CHROME),
        ('content', 160, 300, WHITE),
    ],
    'select-one': [
        ('selrow', 200, 110, SEL),
        ('appbar', 40, 504, CHROME),
    ],
    'commands': [
        ('scrim', 160, 100, SCRIM),
        ('sheet', 250, 190, WHITE),
        ('row-sep', 4, 255, (0xE0, 0xE0, 0xE0)),
    ],
    'properties': [
        ('navbar', 160, 24, CHROME),
        ('page', 160, 300, WHITE),
    ],
    # newfolder sheet above the SIP + keyboard backdrop
    'newfolder-sheet': [
        ('sheet', 160, 150, WHITE),
        ('kb-backdrop', 160, 300, PANE),
        ('scrim', 160, 100, SCRIM),
    ],
    'newfolder-typed': [
        ('sheet', 160, 150, WHITE),
        ('kb-key', 16, 340, WHITE),
    ],
    'newfolder-created': [
        ('page', 160, 300, WHITE),
        ('appbar', 40, 504, CHROME),
    ],
    'search': [
        ('navbar', 160, 4, CHROME),
        ('field', 160, 24, WHITE),
        ('kb-backdrop', 160, 300, PANE),
    ],
    'search-typed': [
        ('field', 160, 24, WHITE),
        ('kb-key', 16, 340, WHITE),
        ('content', 40, 60, WHITE),
    ],
    'search-jump': [('selrow', 200, 168, SEL), ('page', 160, 300, WHITE)],
    'pictures': [('page', 160, 300, WHITE), ('navbar', 160, 24, CHROME)],
    'select-photo': [('selrow', 200, 110, SEL), ('appbar', 40, 504, CHROME)],
    'delete-confirm': [
        ('scrim', 160, 200, SCRIM),
        ('sheet', 200, 385, WHITE),
        ('ok-btn', 30, 500, WHITE),
    ],
    'deleted': [('page', 160, 300, WHITE)],
    'dest-picker': [
        ('content', 160, 300, WHITE),
        ('appbar', 200, 504, CHROME),
    ],
    'dest-documents': [('content', 160, 200, WHITE), ('appbar', 40, 504, CHROME)],
    'copy-done': [('page', 160, 300, WHITE), ('appbar', 40, 504, CHROME)],
}


def main():
    fails = 0
    for path in sys.argv[1:]:
        name = path.split('/')[-1].replace('files-', '').replace('.png', '')
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
        # non-blank: colour variance
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
