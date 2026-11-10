#!/usr/bin/env python3
"""check_edge_shots.py — visual-similarity validation for the Edge POC.

Compares each scripted capture against the real-device reference shots in
refs/edge-screens/ using the agreed block metric: both images are split
into 16x16 blocks, each block reduced to its average colour; a block
matches when every channel is within TOL of the reference. A screen passes
when at least MATCH_MIN of its blocks match (antialiasing and small font
differences average out at block granularity).

Also keeps a few exact pixel probes for structural anchors (address bar
colour, sheet colours) so layout regressions fail loudly.

Stdlib only (the CI runner has no PIL).
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..',
                                'ntos_linux', 'tests'))
from check_shots import read_png  # noqa: E402  -> (w, h, rgb_bytes)


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _unfilter(raw, w, h, bpp):
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        f = raw[pos]
        row = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if f == 0:
            pass
        elif f == 1:
            for i in range(bpp, stride):
                row[i] = (row[i] + row[i - bpp]) & 0xFF
        elif f == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                upl = prev[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + _paeth(left, prev[i], upl)) & 0xFF
        else:
            raise AssertionError(f'bad filter {f}')
        out[y * stride:(y + 1) * stride] = row
        prev = row
    return bytes(out)


def read_any_png(path):
    """read_png for RGB captures and the RGBA reference shots alike."""
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', 'bad signature'
    pos = 8
    idat = b''
    w = h = ctype = None
    while pos < len(d):
        ln, typ = struct.unpack('>I4s', d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            assert depth == 8 and ctype in (2, 6), f'unsupported {depth=} {ctype=}'
        elif typ == b'IDAT':
            idat += body
        pos += 12 + ln
    raw = zlib.decompress(idat)
    bpp = 4 if ctype == 6 else 3
    rgb = _unfilter(raw, w, h, bpp)
    if bpp == 3:
        return w, h, rgb
    out = bytearray()
    for i in range(0, len(rgb), 4):
        out += rgb[i:i + 3]  # drop alpha
    return w, h, bytes(out)

HERE = os.path.dirname(os.path.abspath(__file__))
REF_DIR = os.path.normpath(os.path.join(HERE, '..', '..', '..',
                                        'refs', 'edge-screens'))

BLK = 16
TOL = 48           # per-channel tolerance on block-average colour
MATCH_MIN = 0.80   # >= 80% of blocks must match the reference

# shot name -> reference capture
REFERENCE = {
    'boot': 'home.png',
    'menu': 'context.png',
    'address': 'type.png',
    'wiki': 'webpage.png',
    'readlist': 'add_reading.png',
    'tabs': 'tabs.png',
    'manytabs': 'manytabs.png',
    'clear': 'clean_hist.png',
    'hub-history': 'history.png',
    'hub-downloads': 'edge4.png',
    'inpriv': 'inpriv.png',
    'findbar': 'find next.png',
    'settings': 'settings/settings_1.png',
    'settings-dd': 'settings/settings_1_websitepref_dropdown.png',
    'settings-scrolled': 'settings/settings_1_scrolled.png',
    'settings-scrolled-more': 'settings/settings_1_scrolled_more.png',
    'advanced': 'settings/settings_2_advanced.png',
    'advanced-scrolled': 'settings/settings_2_advanced_scolled.png',
    'advanced-bottom': 'settings/settings_2_advanced_scolled_bottom.png',
    'advanced-cookies-dd': 'settings/settings_2_advanced_scolled_bottom_dropdown_open.png',
    'feedback': 'settings/feedback.png',
}

# structural anchors: (x, y) must be near the given RGB
ANCHORS = {
    'edge-boot': [
        (160, 505, (0x1F, 0x1F, 0x1F)),  # address bar
        (160, 240, (0xFF, 0xFF, 0xFF)),  # white content
    ],
    'edge-menu': [
        (160, 80, (0x7F, 0x7F, 0x7F)),   # scrim
        (160, 200, (0x2B, 0x2B, 0x2B)),  # sheet list
        (160, 430, (0x1F, 0x1F, 0x1F)),  # icon bar
        (160, 478, (0x1F, 0x1F, 0x1F)),  # icon bar reaches the address bar
    ],
    'edge-address': [
        (160, 250, (0xFF, 0xFF, 0xFF)),  # white field
        (160, 300, (0x1F, 0x1F, 0x1F)),  # toolbar row
        (16, 330, (0x33, 0x33, 0x33)),   # a key
    ],
    'edge-clear': [
        (19, 73, (0x00, 0x78, 0xD7)),    # checked box fill
        (160, 200, (0x00, 0x00, 0x00)),  # black page
        (80, 410, (0x33, 0x33, 0x33)),   # Clear button
    ],
    'edge-tabs': [
        (160, 300, (0x00, 0x00, 0x00)),  # black page
        (80, 60, (0x1F, 0x1F, 0x1F)),    # card header strip
        (13, 100, (0x1F, 0x1F, 0x1F)),   # minimized-tab left border
        (154, 100, (0x1F, 0x1F, 0x1F)),  # minimized-tab right border
        (80, 189, (0x1F, 0x1F, 0x1F)),   # minimized-tab bottom border
    ],
    'edge-inpriv': [
        (110, 505, (0x00, 0x78, 0xD7)),  # blue InPrivate glyph in the address bar
    ],
    'edge-settings': [
        (160, 505, (0x00, 0x00, 0x00)),  # nav bar strip (black)
    ],
    'edge-settings-dd': [
        (60, 90, (0x00, 0x78, 0xD7)),    # selected flyout row
    ],
    'edge-advanced': [
        (160, 505, (0x00, 0x00, 0x00)),
    ],
    'edge-advanced-bottom': [
        (160, 505, (0x00, 0x00, 0x00)),
    ],
    'edge-advanced-cookies-dd': [
        (60, 110, (0x00, 0x78, 0xD7)),   # selected "Don't block" row
    ],
    'edge-feedback': [
        (160, 505, (0x00, 0x00, 0x00)),
    ],
}

ATOL = 8  # anchor tolerance per channel

# glyph-presence regions: (x0, y0, x1, y1, min_pixels, threshold, channel)
# channel: 0 red, 1 green, 2 blue; negative threshold = count pixels BELOW
# its absolute value (dark text on white). Guards against "right colours,
# missing text/icons" regressions the block metric alone can hide.
PRESENCE = {
    'boot': [
        (98, 496, 212, 512, 40, 140, 0),   # "Search or browse" placeholder
        (14, 494, 34, 516, 20, 140, 0),    # tabs icon
        (286, 498, 306, 510, 6, 140, 0),   # "..." dots
        (235, 494, 258, 518, 8, 60, 0),    # reader icon (#4C4C4C)
    ],
    'menu': [
        (13, 200, 120, 235, 40, 150, 0),   # "Share" row text
        (13, 380, 200, 415, 60, 150, 0),   # "Pin this page to Start"
        (60, 432, 92, 458, 12, 100, 0),    # star icon (y434-454)
        (40, 460, 300, 478, 40, 150, 0),   # icon-bar labels (y464)
    ],
    'address': [
        (11, 238, 130, 258, 30, 100, 0),   # hint text in the white field
        (14, 282, 36, 306, 15, 100, 0),    # tabs icon on the toolbar row
        (289, 489, 307, 509, 100, 100, 0), # white go key
        (291, 494, 305, 506, 5, -100, 0),  # black go-arrow glyph
    ],
    'clear': [
        (13, 16, 150, 32, 50, 150, 0),     # title
        (20, 396, 170, 420, 30, 150, 0),   # Clear button label
        (12, 290, 310, 320, 30, 150, 0),   # "Passwords" row label
        (12, 452, 306, 526, 20, 150, 2),   # blue links (blue channel)
    ],
    'tabs': [
        (12, 14, 50, 32, 20, 150, 0),      # "Tabs" title
        (42, 52, 134, 68, 15, 100, 0),      # card title (#8A8A8A)
        (12, 75, 14, 185, 60, 20, 0),       # left border column
        (152, 75, 154, 185, 60, 20, 0),     # right border column
        (14, 188, 150, 190, 80, 20, 0),     # bottom border row
        (138, 52, 152, 68, 8, 100, 0),      # close cross (#B0B0B0)
        (162, 496, 180, 512, 15, 100, 0),   # toolbar "+" (#C7C7C7)
        (228, 494, 248, 512, 25, 100, 0),   # InPrivate hatch icon
        (287, 502, 305, 508, 6, 100, 0),    # toolbar "..." dots
    ],
    'hub-history': [
        (12, 58, 90, 76, 25, 150, 0),      # HISTORY title
        (220, 58, 310, 78, 25, 180, 2),    # "Clear all history" blue link
        (44, 140, 300, 175, 60, 150, 0),   # first history row title
    ],
    'inpriv': [
        (36, 128, 222, 150, 30, 150, 0),   # Browsing InPrivate heading
        (36, 180, 270, 300, 100, 150, 0),  # body paragraph
        (36, 342, 250, 360, 25, 180, 2),   # blue link (blue channel)
        (100, 494, 124, 516, 20, 150, 2),  # blue InPrivate address glyph
    ],
    'readlist': [
        (12, 13, 156, 34, 25, 150, 0),     # READING LIST title
        (130, 170, 190, 226, 40, 150, 0),  # white hero glyph
    ],
    'wiki': [
        (16, 6, 90, 34, 40, -100, 0),      # "Exam" title (dark on white)
        (16, 38, 60, 56, 8, 120, 2),       # "Article" link + underline
        (64, 38, 92, 56, 8, 120, 2),       # "Talk" link + underline
        (104, 496, 120, 516, 8, 60, 0),     # lock glyph in the address bar
    ],
    # settings family: state discriminators + shared nav-bar glyphs.
    # The block metric alone cannot tell the settings pages apart, so each
    # screen carries probes for content ONLY that page can show.
    'settings': [
        (12, 12, 100, 32, 15, 200, 0),      # SETTINGS title (white)
        (53, 496, 77, 518, 8, 200, 0),      # nav bar back arrow
        (149, 497, 170, 518, 30, 200, 0),   # nav bar home squares
        (244, 496, 268, 518, 8, 200, 0),    # nav bar search
        (10, 320, 60, 350, 25, 60, 0),      # disabled Sync pill (#4A4A4A)
        (12, 390, 310, 445, 30, 150, 0),    # orange account warning
    ],
    'settings-dd': [
        (14, 70, 300, 145, 300, 180, 2),    # selected blue flyout row
        (24, 105, 200, 140, 10, 200, 0),    # "Desktop version" row text
    ],
    'settings-scrolled': [
        (12, 132, 170, 172, 10, 200, 0),    # Reading section header
        (12, 36, 310, 76, 30, 150, 0),      # orange warning (top of fold)
        (53, 496, 77, 518, 8, 200, 0),      # nav bar back
    ],
    'settings-scrolled-more': [
        (12, 350, 170, 400, 8, 180, 2),     # blue Terms/Privacy links
        (12, 300, 210, 335, 8, 200, 0),     # (c) 2016 Microsoft row
        (53, 496, 77, 518, 8, 200, 0),      # nav bar back
    ],
    'advanced': [
        (12, 110, 300, 142, 20, 200, 0),    # "Ask me what to do with each download"
        (12, 455, 92, 485, 4, 200, 0),      # DNT "Off" label
        (10, 140, 60, 170, 40, 180, 2),     # blue ON toggle pill
        (12, 455, 60, 485, 20, 90, 0),      # OFF toggle outline (dim)
    ],
    'advanced-scrolled': [
        (12, 148, 90, 182, 8, 120, 0),      # "Bing" value (gray #9A9A9A)
        (12, 255, 300, 290, 15, 200, 0),    # Cookies dropdown box row
        (10, 260, 60, 290, 30, 180, 2),     # a blue ON pill below the fold
    ],
    'advanced-bottom': [
        (12, 350, 300, 385, 15, 200, 0),    # SmartScreen label
        (10, 410, 60, 435, 30, 180, 2),     # SmartScreen ON pill
        (53, 496, 77, 518, 8, 200, 0),      # nav bar back
    ],
    'advanced-cookies-dd': [
        (14, 0, 300, 145, 300, 180, 2),     # selected blue flyout row
        (24, 100, 290, 135, 10, 200, 0),    # "Don't block cookies" row text
    ],
    'feedback': [
        (12, 250, 320, 292, 15, 200, 0),    # section header
        (12, 176, 184, 212, 15, 200, 0),    # "Report site issue" button label
        (12, 450, 310, 470, 8, 200, 0),     # bottom note
        (53, 496, 77, 518, 8, 200, 0),      # nav bar back
    ],
    # markdown "web pages" (no device reference: our own text-page engine)
    'mdpage': [
        (16, 10, 100, 36, 25, -100, 0),     # "Exam" heading (dark on white)
        (12, 55, 310, 120, 20, 150, 2),     # blue underlined links
        (134, 320, 186, 380, 100, -300, 0), # svf art in the image slot
        (24, 400, 300, 450, 100, 230, 0),   # fused code-block backdrop (#EFEFEF)
        (12, 55, 310, 120, 4, -200, 0),     # no dark blob over the link zone
    ],
    'mdfind': [
        (0, 0, 320, 225, 500, 200, 0),      # yellow match highlights (red ch)
        (12, 230, 150, 265, 30, -100, 0),   # the typed query in the field
    ],
    'mdfind-kbd': [
        (278, 489, 317, 509, 200, 200, 0),  # white go key
        (289, 493, 307, 507, 10, -100, 0),  # search-magnifier glyph (black)
    ],
    'mdfind-next': [
        (0, 0, 320, 480, 200, 230, 0),      # orange active match (red ch)
        (0, 0, 320, 480, 300, 200, 0),      # yellow matches
    ],
}


class Img:
    def __init__(self, path):
        self.w, self.h, self.px = read_any_png(path)

    def pixel(self, x, y):
        i = (y * self.w + x) * 3
        return (self.px[i], self.px[i + 1], self.px[i + 2])

    def block_avg(self, bx, by):
        rs = gs = bs = n = 0
        for y in range(by, min(by + BLK, self.h)):
            base = y * self.w * 3
            for x in range(bx, min(bx + BLK, self.w)):
                i = base + x * 3
                rs += self.px[i]
                gs += self.px[i + 1]
                bs += self.px[i + 2]
                n += 1
        return (rs // n, gs // n, bs // n)


def similarity(ours_path, ref_path):
    a = Img(ours_path)
    b = Img(ref_path)
    if (a.w, a.h) != (b.w, b.h):
        return None, f'size mismatch {a.w}x{a.h} vs {b.w}x{b.h}'
    total = match = 0
    worst = []
    for by in range(0, a.h, BLK):
        for bx in range(0, a.w, BLK):
            ca = a.block_avg(bx, by)
            cb = b.block_avg(bx, by)
            total += 1
            d = max(abs(ca[i] - cb[i]) for i in range(3))
            if d <= TOL:
                match += 1
            else:
                worst.append((d, (bx, by), ca, cb))
    worst.sort(reverse=True)
    return match / total, worst


def anchors_ok(path, probes):
    im = Img(path)
    bad = []
    for x, y, want in probes:
        if not (0 <= x < im.w and 0 <= y < im.h):
            bad.append((x, y, want, None))
            continue
        got = im.pixel(x, y)
        if max(abs(got[i] - want[i]) for i in range(3)) > ATOL:
            bad.append((x, y, want, got))
    return bad


def presence_ok(path, regions):
    """regions: (x0, y0, x1, y1, min_count, thr, channel)."""
    im = Img(path)
    bad = []
    for x0, y0, x1, y1, need, thr, ch in regions:
        n = 0
        below = thr < 0
        t = abs(thr)
        for y in range(y0, min(y1, im.h)):
            base = y * im.w * 3
            for x in range(x0, min(x1, im.w)):
                v = im.px[base + x * 3 + ch]
                if (below and v < t) or (not below and v > t):
                    n += 1
        if n < need:
            bad.append((x0, y0, x1, y1, need, n))
    return bad


def main(paths):
    failures = 0
    for p in paths:
        name = os.path.basename(p)[len('edge-'):-len('.png')]
        ref = REFERENCE.get(name)
        status = []
        if ref:
            ref_path = os.path.join(REF_DIR, ref)
            if not os.path.exists(ref_path):
                print(f'{p}: MISSING reference {ref}')
                failures += 1
                continue
            ratio, worst = similarity(p, ref_path)
            if ratio is None:
                print(f'{p}: {worst}')
                failures += 1
                continue
            status.append(f'{ratio * 100:.1f}% blocks match {ref} '
                          f'(need {MATCH_MIN * 100:.0f}%)')
            if ratio < MATCH_MIN:
                failures += 1
                status.append('FAIL')
                for d, (bx, by), ca, cb in worst[:5]:
                    status.append(f'   worst block ({bx},{by}) '
                                  f'ours{ca} ref{cb} d={d}')
            else:
                status.append('ok')
        for x, y, want, got in anchors_ok(p, ANCHORS.get(name, [])):
            failures += 1
            print(f'{p}: anchor ({x},{y}) want {want} got {got}')
        for x0, y0, x1, y1, need, got in presence_ok(p, PRESENCE.get(name, [])):
            failures += 1
            print(f'{p}: glyphs missing in ({x0},{y0})-({x1},{y1}): '
                  f'{got} px, need {need}')
        print(f'{p}: ' + '; '.join(status) if status
              else f'{p}: no reference (state-only)')
    if failures:
        print(f'{failures} failure(s)')
        return 1
    print('all edge shot checks passed')
    return 0


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('usage: check_edge_shots.py edge-*.png')
        sys.exit(2)
    sys.exit(main(sys.argv[1:]))
