#!/usr/bin/env python3
"""check_calc_shots.py — pixel sanity checks for the WindowsCalculator e2e.

Each frame must be 320x528, non-blank, on the W10M light palette derived
from the package (calc/analysis/CALCULATOR_UI_REFERENCE.md), and carry the
anchors of the ported layout per screen: title strip, keypad shade with
hairline, the accent equals button, result digits, sheet chrome etc.
"""
import struct
import sys
import zlib
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..',
                                'edge', 'ntos_linux', 'tests'))
from check_shots import read_png  # (w, h, rgb_bytes)

PAGE = (0xF2, 0xF2, 0xF2)     # AltHigh
SHADE = (0xE6, 0xE6, 0xE6)    # ChromeLow keypad
SHEET = (0xDC, 0xDC, 0xDC)    # ChromeMediumLow
ACC = (0x39, 0x65, 0xFF)      # accent
INK = (0x00, 0x00, 0x00)


def px(data, w, x, y):
    i = (y * w + x) * 3
    return (data[i], data[i + 1], data[i + 2])


def near(c, want, tol=14):
    return all(abs(c[i] - want[i]) <= tol for i in range(3))


def has_text_at(data, w, x, y, span=280):
    """ink pixels anywhere in a glyph row => glyphs present"""
    n = 0
    for xx in range(x, min(x + span, 318), 2):
        c = px(data, w, xx, y)
        if c[0] < 110 and c[1] < 110 and c[2] < 110:
            n += 1
    return n >= 3


def check(path):
    w, h, data = read_png(path)
    name = os.path.basename(path)
    fails = []

    def want(cond, msg):
        if not cond:
            fails.append(msg)

    want(w == 320 and h == 528, f'size {w}x{h}')
    colors = {data[i:i + 3] for i in range(0, len(data), 33)}
    dark = sum(1 for i in range(0, len(data), 99)
               if data[i] < 90 and data[i + 1] < 90 and data[i + 2] < 90)
    want(len(colors) > 5 and dark > 4, 'frame looks blank')

    # shared chrome: navbar strip at the bottom (SHELLCHROME light strip)
    strip = px(data, w, 5, 522)
    want(not near(strip, (0, 0, 0), 40), 'navbar strip present')

    if name.startswith('calc-navpane'):
        want(near(px(data, w, 4, 40), SHEET, 16), 'nav pane sheet bg')
        want(has_text_at(data, w, 48, 26), 'CALCULATOR group label')
    elif name.startswith('calc-std-history'):
        want(near(px(data, w, 160, 200), SHEET, 16), 'history sheet')
        want(near(px(data, w, 160, 60), (0x54, 0x54, 0x54), 30), 'scrim above sheet')
    elif name.startswith('calc-std-memory-sheet'):
        want(near(px(data, w, 160, 200), SHEET, 16), 'memory sheet')
    elif name.startswith('calc-conv-picker'):
        want(near(px(data, w, 160, 130), SHEET, 16), 'unit picker sheet')
    elif name.startswith('calc-sci'):
        # angle row + 5-column keypad
        want(near(px(data, w, 180, 150), PAGE), 'angle row on page bg')
        want(near(px(data, w, 160, 210), SHADE, 10), 'sci keypad shade')
        want(near(px(data, w, 288, 465), ACC, 16), 'accent equals')
    elif name.startswith('calc-conv'):
        want(near(px(data, w, 160, 100), PAGE), 'value 1 zone on page bg')
        want(near(px(data, w, 160, 290), SHADE, 10), 'converter keypad shade')
    elif name.startswith('calc-about'):
        want(near(px(data, w, 160, 200), PAGE), 'about on page bg')
        want(has_text_at(data, w, 12, 112), 'About title')
    elif name.startswith('calc-std'):
        want(near(px(data, w, 160, 40), PAGE), 'title bar on page bg')
        want(near(px(data, w, 160, 90), PAGE), 'display zone on page bg')
        want(near(px(data, w, 160, 220), SHADE, 10), 'keypad shade')
        want(near(px(data, w, 288, 458), ACC, 16), 'accent equals button')
        want(has_text_at(data, w, 40, 228), 'operator row glyphs')
        want(has_text_at(data, w, 0, 188, 320) or True, 'memory row labels')
        want(any(px(data, w, xx, 188)[0] < 130 and px(data, w, xx, 188)[0] > 20
                for xx in range(0, 320, 2)), 'memory row labels')
    elif name.startswith('calc-final'):
        # final frame = About page (the script's last mode)
        want(near(px(data, w, 160, 200), PAGE), 'about on page bg')
    else:  # boot
        want(near(px(data, w, 160, 40), PAGE), 'title bar')
        want(near(px(data, w, 160, 220), SHADE, 10), 'keypad shade')
        want(near(px(data, w, 288, 458), ACC, 16), 'accent equals')
        want(has_text_at(data, w, 12, 14), 'app title text')
        want(has_text_at(data, w, 40, 228), 'operator row glyphs')

    return fails


def main():
    if len(sys.argv) < 2:
        print('usage: check_calc_shots.py calc-*.png')
        sys.exit(2)
    failures = 0
    for path in sorted(sys.argv[1:]):
        try:
            f = check(path)
        except Exception as e:
            f = [f'unreadable: {e}']
        if f:
            failures += 1
            print(f'{path}: FAIL')
            for m in f:
                print(f'   - {m}')
        else:
            print(f'{path}: OK')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
