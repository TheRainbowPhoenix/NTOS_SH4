#!/usr/bin/env python3
"""check_settings_shots.py — pixel sanity checks for the SystemSettings e2e.

Verifies each screenshot is the right size, non-blank, uses the W10M light
palette measured on refs/settings-screens and — per screen — the specific
anchors of the ported layout (header gear+title, accent search border,
category rows, deep-dive controls, picker band, SIP, navbar strip).
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


def win_has(px, x, y, want, tol=18, r=4):
    """True when any pixel in the (2r+1)^2 window around (x, y) is near
    the wanted colour — text/1px-stroke glyphs never fill a whole pixel
    at an exact coordinate."""
    for yy in range(max(0, y - r), min(528, y + r + 1)):
        row = px[yy]
        for xx in range(max(0, x - r), min(320, x + r + 1)):
            c = (row[xx * 3], row[xx * 3 + 1], row[xx * 3 + 2])
            if near(c, want, tol):
                return True
    return False


PAGE = (0xF7, 0xF3, 0xF7)      # header/home bg
WHITE = (255, 255, 255)
INK = (0, 0, 0)
ACC = (0x39, 0x65, 0xFF)       # accent
MUT = (0x63, 0x61, 0x63)       # secondary text
BTN = (0xCE, 0xCF, 0xCE)       # action buttons / toggle off / navbar
EDGE = (0x9C, 0x9A, 0x9C)      # combobox border
BAND = (0xAD, 0xB6, 0xF7)      # picker/combo selected band
SCRIM = (0x7F, 0x7F, 0x7F)
KBBG = (0xF2, 0xF2, 0xF2)      # SIP surface
TRACK = (0x46, 0x45, 0x46)     # slider unfilled
SCROLL = (0xC6, 0xC3, 0xC6)

CHECKS = {
    'boot': [
        ('page-bg', 160, 30, PAGE),
        ('accent-line', 160, 57, ACC),
        ('search-border', 160, 62, ACC),
        ('search-fill', 160, 70, WHITE),
        ('row1-icon-blue', 21, 120, ACC),
        ('row1-title-ink', 60, 112, INK),
        ('row1-desc-mut', 70, 134, MUT),
        ('scrollbar', 317, 200, SCROLL),
    ],
    'home-scrolled': [
        ('page-bg', 160, 30, PAGE),
        ('accent-line', 160, 57, ACC),
        ('row-icon-blue', 28, 128, ACC),   # Accounts person head
        ('scrollbar', 317, 300, SCROLL),
    ],
    'personalization': [
        ('page-bg', 160, 30, PAGE),
        ('accent-line', 160, 57, ACC),
        ('heading-mut', 13, 108, MUT),     # "Personalization" P stem
        ('row-icon-blue', 22, 150, ACC),   # Start row icon
        ('row-title', 60, 150, INK),       # "Start" S stem
    ],
    'colors': [
        ('content-white', 160, 90, WHITE),
        ('mode-heading', 13, 78, MUT),
        ('radio-ring', 35, 106, MUT),      # Dark radio ring (r=9)
        ('swatch-gold', 30, 220, (0xFF, 0xB9, 0x00)),
        ('swatch-orange', 80, 220, (0xF7, 0x63, 0x0C)),
        ('accent-heading', 13, 178, MUT),
    ],
    'sounds': [
        ('content-white', 160, 160, WHITE),
        ('ringtone-heading', 13, 80, MUT),
        ('combo-border', 160, 94, EDGE),
        ('combo-white', 160, 110, WHITE),
        ('link-blue', 70, 146, ACC),
        ('toggle-on', 30, 204, ACC),       # vibrate ON (blue pill)
        ('checkbox-blue', 30, 274, ACC),   # Key press checked
    ],
    'sounds-set': [
        ('content-white', 160, 300, WHITE),
        ('toggle-off-btn', 30, 204, BTN),  # vibrate now OFF
        ('checkbox-off-white', 30, 306, WHITE),  # Lock and unlock unchecked
        ('checkbox-blue', 30, 338, ACC),   # Camera shutter still checked
    ],
    'start': [
        ('content-white', 160, 90, WHITE),
        ('preview-tile', 110, 120, ACC),
        ('preview-frame', 94, 150, INK),
        ('combo-border', 12, 336, EDGE),
        ('combo-value', 28, 334, INK),     # "None"
    ],
    'start-flyout': [
        ('scrim', 160, 80, SCRIM),         # shard covers the title bar
        ('panel-white', 160, 340, WHITE),
        ('sel-band', 160, 398, BAND),      # None selected row
    ],
    'start-picked': [
        ('content-white', 160, 90, WHITE),
        ('combo-value', 28, 334, INK),     # now "Sample images"
    ],
    'system': [
        ('page-bg', 160, 30, PAGE),
        ('accent-line', 160, 57, ACC),
        ('heading-mut', 13, 108, MUT),
        ('display-row', 60, 150, INK),
        ('row-icon-blue', 22, 150, ACC),
    ],
    'display': [
        ('header-bg', 160, 30, PAGE),
        ('content-white', 160, 300, WHITE),
        ('slider-track-right', 250, 106, TRACK),
        ('slider-fill', 100, 106, ACC),
        ('slider-thumb', 145, 106, ACC),
        ('toggle-on', 30, 214, ACC),
        ('toggle2-off', 30, 296, BTN),
        ('apply-btn', 60, 460, BTN),
    ],
    'display-set': [
        ('slider-thumb-moved', 234, 106, ACC),
        ('toggle-off-btn', 30, 214, BTN),  # auto brightness now off
        ('toggle2-off', 30, 296, BTN),
    ],
    'about': [
        ('content-white', 160, 300, WHITE),
        ('devname-heading', 13, 80, INK),
        ('devname-value', 30, 104, INK),
        ('edit-btn', 60, 136, BTN),
        ('model-label', 30, 212, MUT),
        ('model-value', 70, 212, INK),
        ('ram-row', 30, 296, MUT),
    ],
    'editname': [
        ('page-bg', 160, 100, PAGE),
        ('field-border', 160, 60, ACC),
        ('field-text', 34, 74, INK),       # "Windows phone"
        ('save-btn', 90, 140, BTN),
        ('kb-surface', 160, 300, KBBG),
    ],
    'editname-typed': [
        ('field-border', 160, 60, ACC),
        ('field-text', 34, 74, INK),       # "My"
        ('kb-surface', 160, 300, KBBG),
    ],
    'about-renamed': [
        ('content-white', 160, 300, WHITE),
        ('devname-heading', 13, 80, INK),
        ('devname-value', 30, 104, INK),   # "My"
    ],
    'usb': [
        ('header-bg', 160, 30, PAGE),
        ('content-white', 160, 130, WHITE),
        ('page-title', 16, 84, INK),       # "USB" U stem
        ('toggle-off-btn', 30, 226, BTN),
    ],
    'usb-on': [
        ('toggle-on', 30, 226, ACC),
        ('toggle2-on', 30, 334, ACC),
    ],
    'update': [
        ('page-bg', 160, 30, PAGE),
        ('accent-line', 160, 57, ACC),
        ('heading-mut', 13, 108, MUT),     # "Update & security"
        ('row-title', 60, 150, INK),
    ],
    'activehours': [
        ('header-bg', 160, 30, PAGE),
        ('content-white', 160, 160, WHITE),
        ('time-box-border', 12, 265, EDGE),
        ('time-hour', 40, 262, INK),
        ('save-btn', 90, 390, BTN),
    ],
    'picker': [
        ('picker-border', 24, 200, EDGE),
        ('picker-band', 160, 260, BAND),
        ('confirm-btn', 100, 440, BTN),
    ],
    'activehours-set': [
        ('content-white', 160, 160, WHITE),
        ('time-box-border', 12, 265, EDGE),
    ],
    'search': [
        ('page-bg', 160, 30, PAGE),
        ('accent-line', 160, 57, ACC),
        ('field-border', 160, 62, ACC),
        ('suggest-strip', 300, 110, (0xE5, 0xE1, 0xE5)),
        ('kb-surface', 160, 300, KBBG),
    ],
    'search-te': [
        ('field-border', 160, 62, ACC),
        ('suggest-word', 18, 110, INK),    # "test"
        ('result-icon', 24, 144, ACC),
        ('kb-surface', 160, 300, KBBG),
    ],
    'search-accepted': [
        ('page-bg', 160, 30, PAGE),        # back on the Date & time L2
        ('accent-line', 160, 57, ACC),
        ('heading-mut', 13, 108, MUT),
    ],
    'home-final': [
        ('page-bg', 160, 30, PAGE),
        ('accent-line', 160, 57, ACC),
        ('row1-icon-blue', 21, 120, ACC),
        ('search-border', 160, 62, ACC),
    ],
}

# navbar strip present on every screen (empty bar area, clear of glyphs)
NAVBAR = ('navbar', 100, 490, BTN)


def main():
    fails = 0
    for path in sys.argv[1:]:
        name = path.split('/')[-1].replace('settings-', '').replace('.png', '')
        try:
            w, h, px = read_png(path)
        except Exception as e:
            print(f'{path}: READ FAIL {e}')
            fails += 1
            continue
        if (w, h) != (320, 528):
            print(f'{path}: size {w}x{h} (want 320x528)')
            fails += 1
            continue
        ink = 0
        varied = set()
        for y in range(0, h, 8):
            for x in range(0, w, 8):
                c = pix(px, x, y)
                if sum(c) < 420:
                    ink += 1
                varied.add(c)
        if ink < 12 or len(varied) < 5:
            print(f'{path}: nearly blank (ink={ink}, colors={len(varied)})')
            fails += 1
            continue
        probes = CHECKS.get(name)
        if not probes:
            if name != 'final':
                print(f'{path}: no probes (skipped details)')
            continue
        bad = 0
        for label, x, y, want in probes + [NAVBAR]:
            if not win_has(px, x, y, want):
                c = pix(px, x, y)
                print(f'{path}: {label} @({x},{y}) = {c}, want {want}')
                bad += 1
        if bad:
            fails += 1
        else:
            print(f'{path}: {len(probes) + 1} probes [OK]')
    if fails:
        sys.exit(1)
    print('all screenshots verified')


if __name__ == '__main__':
    main()
