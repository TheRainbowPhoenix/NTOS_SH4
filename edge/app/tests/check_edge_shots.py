#!/usr/bin/env python3
"""check_edge_shots.py — pixel-precise validation for the Edge POC frames.

Each frame gets region checks (exact fill colours at safe coordinates) on top
of the generic non-blank check. Colours are PNG RGB (the host LCD shuffles
device 0x00BBGGRR to RGB on output). Tolerance +-2 per channel.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..',
                                'ntos_linux', 'tests'))
from check_shots import read_png  # noqa: E402

BLACK = (0x00, 0x00, 0x00)
SHEET = (0x2B, 0x2B, 0x2B)   # menus, cards, flyouts
CARD = (0x33, 0x33, 0x33)    # tiles, keyboard keys
SEL = (0x3A, 0x3A, 0x3A)     # selected tab card
FIELD = (0x1F, 0x1F, 0x1F)   # inputs, news cards, toast
SEP = (0x46, 0x46, 0x46)
BLUE = (0x00, 0x78, 0xD7)
SCRIM = (0x14, 0x14, 0x14)
WHITE = (0xFF, 0xFF, 0xFF)

# name -> [(x, y, (r, g, b)), ...]
EXPECT = {
    'edge-boot': [
        (150, 486, BLACK),   # bottom address bar background
        (40, 56, SHEET),     # NTP top-site plate (row 1)
        (274, 160, SHEET),   # NTP top-site plate (row 2 col 4)
        (160, 260, FIELD),   # news card surface
        (160, 200, BLACK),   # gap between tile rows
    ],
    'edge-address': [
        (100, 28, SHEET),    # address field background
        (160, 150, BLACK),   # suggestions area
        (30, 400, CARD),     # SIP key (row 2)
        (232, 476, BLUE),    # enter key (left of the arrow glyph)
    ],
    'edge-wiki': [
        (160, 40, WHITE),    # article page background
        (300, 100, WHITE),
        (150, 486, BLACK),   # bottom bar
        (16, 508, BLACK),
    ],
    'edge-menu': [
        (160, 60, SCRIM),    # scrim above the sheet
        (160, 200, SHEET),   # sheet: options list
        (160, 470, SHEET),   # sheet: hub icons row
        (10, 100, SCRIM),
    ],
    'edge-readlist': [
        (160, 60, SCRIM),    # scrim
        (250, 180, SHEET),   # dialog card (right of the title text)
        (160, 240, FIELD),   # Name field
        (160, 340, SHEET),   # Cancel button area
    ],
    'edge-settings': [
        (160, 30, BLACK),    # header
        (160, 80, BLACK),    # row background
        (160, 104, FIELD),   # hairline separator
        (274, 80, BLUE),     # sync toggle (on)
    ],
    'edge-clear': [
        (160, 30, BLACK),
        (26, 86, BLUE),      # checkbox 1 checked (Browsing history)
        (26, 218, BLACK),    # checkbox 4 unchecked (Download history)
        (30, 364, BLUE),     # Clear button (left of label)
    ],
    'edge-clear-toggled': [
        (26, 218, BLUE),     # Download history now checked
        (30, 364, BLUE),
        (26, 86, BLUE),
    ],
    'edge-clear-toast': [
        (160, 440, FIELD),   # notification bar
        (30, 364, BLUE),
    ],
    'edge-hub-history': [
        (160, 80, BLACK),
        (200, 99, BLUE),     # pivot underline (History selected)
        (300, 190, BLACK),   # row area background
    ],
    'edge-hub-favorites': [
        (40, 99, BLUE),      # pivot underline (Favorites selected)
        (24, 130, CARD),     # first favorite plate (Wikipedia #333333)
        (300, 190, BLACK),
    ],
    'edge-tabcenter': [
        (160, 30, BLACK),    # header
        (40, 100, SEL),      # single card, selected
        (238, 100, BLACK),   # no second card
    ],
    'edge-tabcenter2': [
        (40, 100, SHEET),    # card 0 unselected
        (238, 100, SEL),     # card 1 selected (new tab)
        (160, 30, BLACK),
    ],
    'edge-tabsmenu': [
        (160, 300, SHEET),   # flyout
        (40, 300, BLACK),    # tab center behind (no card there)
        (300, 200, SHEET),
    ],
    'edge-tabcenter3': [
        (40, 100, SHEET),    # card 0 unselected
        (238, 100, SHEET),   # card 1 unselected
        (100, 190, SEL),     # card 2 selected (duplicate): bg right of plate
    ],
    'edge-final': [
        (160, 40, WHITE),    # back on the wikipedia article
        (150, 486, BLACK),
    ],
}


def close(a, b):
    return all(abs(x - y) <= 2 for x, y in zip(a, b))


def main():
    failures = 0
    for path in sys.argv[1:]:
        base = os.path.basename(path)
        name = base[:-4] if base.endswith('.png') else base
        w, h, px = read_png(path)
        colors = {px[i:i + 3] for i in range(0, len(px), 3)}
        problems = []
        if len(colors) <= 3:
            problems.append(f'blank? only {len(colors)} colors')
        checks = EXPECT.get(name, [])
        if not checks:
            problems.append('NO EXPECTATIONS DEFINED')
        for x, y, want in checks:
            got = (px[(y * w + x) * 3], px[(y * w + x) * 3 + 1],
                   px[(y * w + x) * 3 + 2])
            if not close(got, want):
                problems.append(f'({x},{y}) got {got} want {want}')
        status = 'OK' if not problems else 'FAIL'
        print(f'{base}: {w}x{h}, {len(colors)} colors, '
              f'{len(checks)} px checks [{status}]')
        for p in problems:
            print(f'    {p}')
        if problems:
            failures += 1
    if failures:
        sys.exit(1)


if __name__ == '__main__':
    main()
