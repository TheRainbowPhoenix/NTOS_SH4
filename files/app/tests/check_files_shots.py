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

# the label strip under the app-bar glyphs (y518..523) must be pure chrome
# when the bar is collapsed: icons only, labels hidden by default
def labels_hidden(px):
    for x in (32, 96, 160, 224, 288):
        for y in (518, 521, 523):
            if not near(pix(px, x, y), CHROME):
                return False
    return True


def labels_visible(px):
    # "New folder" spans ~x64..120 at y500..511; sample the label band
    hits = 0
    for x in range(66, 124, 4):
        for y in range(500, 512, 3):
            c = pix(px, x, y)
            if near(c, INK, 60):
                hits += 1
    return hits >= 2


CHECKS = {
    'boot': [
        ('navbar', 160, 24, CHROME),
        ('page', 160, 300, WHITE),
        ('appbar', 40, 504, CHROME),
        ('folder-amber', 24, 116, AMBER),   # VEC folder front panel, row 0
    ],
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
    'sortflyout': [
        ('flyout-panel', 200, 100, WHITE),
        ('divider', 100, 119, (0xE0, 0xE0, 0xE0)),
        ('page-below', 260, 300, WHITE),
    ],
    'sortdesc': [
        ('page', 160, 300, WHITE),
        ('appbar', 40, 504, CHROME),
    ],
    'sortasc': [
        ('page', 160, 300, WHITE),
        ('appbar', 40, 504, CHROME),
    ],
    'overflow-browse': [
        ('scrim', 160, 200, SCRIM),
        ('panel', 160, 340, WHITE),
        ('expanded-bar', 10, 465, CHROME),
    ],
    'props-root': [
        ('navbar', 160, 24, CHROME),
        ('page', 160, 300, WHITE),
    ],
    'selectmode': [
        ('appbar', 40, 504, CHROME),
        ('content', 160, 300, WHITE),
    ],
    'overflow-select': [
        ('scrim', 160, 200, SCRIM),
        ('panel', 160, 340, WHITE),
        ('expanded-bar', 10, 465, CHROME),
    ],
    'rename-sheet': [
        ('sheet', 160, 150, WHITE),
        ('kb-backdrop', 160, 300, PANE),
        ('scrim', 160, 100, SCRIM),
    ],
    'rename-typed': [
        ('sheet', 160, 150, WHITE),
        ('kb-key', 16, 340, WHITE),
    ],
    'rename-done': [
        ('page', 160, 300, WHITE),
        ('appbar', 40, 504, CHROME),
    ],
    'dest-picker': [
        ('content', 160, 300, WHITE),
        ('appbar', 40, 504, CHROME),
    ],
    'dest-documents': [('content', 160, 200, WHITE), ('appbar', 40, 504, CHROME)],
    'dest-copy': [('content', 160, 300, WHITE), ('appbar', 40, 504, CHROME)],
    'move-done': [('page', 160, 300, WHITE), ('appbar', 40, 504, CHROME)],
    'copy-done': [('page', 160, 300, WHITE), ('appbar', 40, 504, CHROME)],
    'scrolled': [
        ('page', 160, 300, WHITE),
        ('scrollbar', 318, 220, (0xC8, 0xC8, 0xC8)),
    ],
    'scrolled-top': [
        ('page', 160, 300, WHITE),
        ('appbar', 40, 504, CHROME),
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
