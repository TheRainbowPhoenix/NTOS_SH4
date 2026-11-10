#!/usr/bin/env python3
"""check_files_shots.py — pixel sanity checks for the File Explorer e2e.

Verifies each screenshot is the right size, non-blank, uses the expected
palette for the W10M light theme and — per screen — the specific anchors
of the v2 layout: icon-only app bar (the label strip under the glyphs must
stay chrome-colour), labels-expanded "..." state, sort flyout, overflow
menu, destination picker and the file:// hand-off shot (Edge chrome).
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
AMBER = (0xF5, 0xC2, 0x42)      # VEC folder front panel (reference tone)
EDGE_BAR = (0xCE, 0xCF, 0xCE)   # edge light navbar (shellchrome NAV_BG)
NAVBG = (0xCE, 0xCF, 0xCE)      # files navbar strip (shellchrome NAV_BG)
NAVINK = (0x31, 0x30, 0x31)     # navbar glyph ink (NAV_INK)

# the label strip under the app-bar glyphs (y475..486) must be pure chrome
# when the bar is collapsed: icons only, labels hidden by default
def labels_hidden(px):
    for x in (32, 96, 160, 224, 288):
        for y in (478, 481, 484):
            if not near(pix(px, x, y), CHROME):
                return False
    return True


def labels_visible(px):
    # labels sit centered under their icons (button centers 32/96/160/224)
    # in the y460..471 band of the expanded bar (y420..488)
    cols = 0
    for cx in (32, 96, 160, 224, 288):
        hits = 0
        for x in range(cx - 12, cx + 13, 3):
            for y in range(460, 472, 2):
                c = pix(px, x, y)
                if near(c, INK, 60):
                    hits += 1
        if hits >= 2:
            cols += 1
    return cols >= 3

# the SHELLCHROME navbar owns y488..528 on every files screen: light strip
# with the Back/Home/Search glyphs (Back = horizontal stroke y508)
def navbar_ok(px):
    for x in (40, 160, 280):
        if not near(pix(px, x, 510), NAVBG):
            return False
    return near(pix(px, 65, 508), NAVINK, 40)


CHECKS = {
    'boot': [
        ('topbar', 160, 24, CHROME),
        ('page', 160, 300, WHITE),
        ('appbar', 40, 464, CHROME),
        ('folder-amber', 44, 116, AMBER),   # VEC folder front panel, row 0
    ],
    'navpane': [
        ('pane', 120, 300, PANE),
        ('scrim', 300, 300, SCRIM),
        ('title-band', 120, 20, PANE),
    ],
    'documents': [
        ('topbar', 160, 24, CHROME),
        ('page', 160, 300, WHITE),
        ('appbar', 40, 464, CHROME),
    ],
    'sortflyout': [
        ('flyout-panel', 200, 100, WHITE),
        ('divider', 100, 119, (0xE0, 0xE0, 0xE0)),
        ('flyout-border', 8, 200, (0xE0, 0xE0, 0xE0)),
        ('page-below', 260, 310, WHITE),
    ],
    'sortdesc': [
        ('page', 160, 300, WHITE),
        ('appbar', 40, 464, CHROME),
    ],
    'sortasc': [
        ('page', 160, 300, WHITE),
        ('appbar', 40, 464, CHROME),
    ],
    'overflow-browse': [
        ('scrim', 160, 200, SCRIM),
        ('panel', 160, 340, WHITE),
        ('expanded-bar', 10, 425, CHROME),
    ],
    'props-root': [
        ('topbar', 160, 24, CHROME),
        ('page', 160, 300, WHITE),
    ],
    'selectmode': [
        ('appbar', 40, 464, CHROME),
        ('content', 160, 300, WHITE),
    ],
    'overflow-select': [
        ('scrim', 160, 200, SCRIM),
        ('panel', 160, 340, WHITE),
        ('expanded-bar', 10, 425, CHROME),
    ],
    'confirm': [
        ('sheet', 160, 120, WHITE),
        ('scrim', 160, 300, SCRIM),
        ('accent-line', 160, 234, ACCENT),
        ('appbar-live', 40, 464, CHROME),
    ],
    'delete-done': [
        ('page', 160, 300, WHITE),
        ('appbar', 40, 464, CHROME),
    ],
    'rename-sheet': [
        # top sheet 48..228 + docked SIP 231..488 + navbar: no scrim shows
        ('sheet', 160, 120, WHITE),
        ('sheet-title', 100, 66, WHITE),
        ('kb-backdrop', 160, 240, PANE),
        ('accent-line', 160, 226, ACCENT),
    ],
    'rename-typed': [
        ('sheet', 160, 120, WHITE),
        ('kb-key', 49, 302, WHITE),
    ],
    'rename-done': [
        ('page', 160, 300, WHITE),
        ('appbar', 40, 464, CHROME),
    ],
    'dest-picker': [
        ('content', 160, 300, WHITE),
        ('appbar', 40, 464, CHROME),
    ],
    'dest-documents': [('content', 160, 200, WHITE), ('appbar', 40, 464, CHROME)],
    'dest-copy': [('content', 160, 300, WHITE), ('appbar', 40, 464, CHROME)],
    'move-done': [('page', 160, 300, WHITE), ('appbar', 40, 464, CHROME)],
    'copy-done': [('page', 160, 300, WHITE), ('appbar', 40, 464, CHROME)],
    'scrolled': [
        ('page', 160, 300, WHITE),
        ('scrollbar', 318, 220, (0xC8, 0xC8, 0xC8)),
    ],
    'scrolled-top': [
        ('page', 160, 300, WHITE),
        ('appbar', 40, 464, CHROME),
    ],
    # file:// activation: Edge takes the foreground (dark chrome, white
    # md content) — the files light theme is gone
    'edge-open': [
        ('edge-bar', 160, 490, EDGE_BAR),
        ('edge-content', 160, 240, WHITE),
    ],
    'final': [
        ('edge-bar', 160, 490, EDGE_BAR),
    ],
}

# screens where the app bar must be icon-only (label strip pure chrome)
ICON_ONLY = {'boot', 'documents', 'sortflyout', 'sortdesc',
             'sortasc', 'selectmode', 'dest-picker',
             'dest-documents', 'dest-copy', 'rename-done', 'move-done',
             'copy-done', 'scrolled', 'scrolled-top'}
# screens where the labels are expanded (the "..." state)
LABELS_OUT = {'overflow-browse', 'overflow-select'}


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
        if name in ICON_ONLY and not labels_hidden(px):
            print(f'{path}: app-bar labels visible while collapsed')
            ok = False
        if name in LABELS_OUT and not labels_visible(px):
            print(f'{path}: expanded app-bar labels missing')
            ok = False
        if name not in ('edge-open', 'final') and not navbar_ok(px):
            print(f'{path}: system navbar strip missing at the bottom')
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
