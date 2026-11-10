#!/usr/bin/env python3
"""check_calc_shots.py — pixel checks for the WindowsCalculator e2e.

Each frame must be 320x528 with the launcher-owned chrome (status bar
y0-24 white + nav bar y480-528 #CECFCE, both from SHELLCHROME), the
capture-calibrated layout (display white, keypad shade #F7F3F7 from the
CE row, hairlines #E7E7E7), and the anchors of each screen state:
result/memory/angle rows, the nav pane selected band, converter accents.
"""
import struct
import sys
import zlib
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..',
                                'edge', 'ntos_linux', 'tests'))
from check_shots import read_png  # (w, h, rgb_bytes)

WHITE = (255, 255, 255)
PAGE = (247, 243, 247)     # F7F3F7 numpad shade (capture)
NAV = (206, 207, 206)      # CECFCE nav strip
HAIR = (231, 231, 231)     # E7E7E7 hairlines
PANE = (231, 227, 231)     # E7E3E7 nav pane / scrim
ACC = (57, 101, 255)       # accent
INK = (0, 0, 0)


def px(data, w, x, y):
    i = (y * w + x) * 3
    return (data[i], data[i + 1], data[i + 2])


def near(c, want, tol=14):
    return all(abs(c[i] - want[i]) <= tol for i in range(3))


def ink_at(data, w, x0, x1, y0, y1, thr=170):
    """dark pixels anywhere in the box => glyph/label present"""
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            c = px(data, w, x, y)
            if c[0] < thr and c[1] < thr and c[2] < thr:
                n += 1
    return n


def col_at(data, w, x0, x1, y0, y1, want, tol=16, min_n=4):
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            if near(px(data, w, x, y), want, tol):
                n += 1
    return n >= min_n


def check(path):
    fails = []

    def want(cond, msg):
        if not cond:
            fails.append(msg)

    w, h, data = read_png(path)
    want(w == 320 and h == 528, f"size {w}x{h}")

    # ---- launcher chrome on every frame ----
    want(near(px(data, w, 160, 4), WHITE, 8), "status bar bg")
    want(ink_at(data, w, 13, 27, 7, 17, 140) >= 4, "status signal bars")
    want(ink_at(data, w, 246, 269, 7, 16, 140) >= 4, "status battery")
    want(ink_at(data, w, 271, 307, 8, 16, 140) >= 6, "status clock")
    want(near(px(data, w, 120, 500), NAV, 10), "nav bar bg")
    want(ink_at(data, w, 55, 76, 495, 513, 120) >= 6, "nav back arrow")
    want(ink_at(data, w, 150, 170, 494, 513, 120) >= 8, "nav home glyph")
    want(ink_at(data, w, 246, 267, 497, 514, 120) >= 6, "nav search glyph")

    name = os.path.basename(path)
    sheet_frame = name in ("calc-std-history.png", "calc-std-memory-sheet.png",
                           "calc-conv-picker.png")
    std = (name.startswith("calc-std-") or name in (
        "calc-boot.png", "calc-navpane.png")) and not sheet_frame
    sci = name.startswith("calc-sci-")
    conv = name.startswith("calc-conv-")
    prog = name.startswith("calc-prog-")

    if std and name != "calc-navpane.png":
        want(near(px(data, w, 160, 40), WHITE, 8), "display area white")
        want(near(px(data, w, 160, 184), HAIR, 10), "hairline y184")
        want(near(px(data, w, 160, 210), WHITE, 8), "% row on white")
        want(near(px(data, w, 160, 300), PAGE, 10), "keypad shade")
        want(near(px(data, w, 160, 450), PAGE, 10), "keypad shade (row 5)")
        want(ink_at(data, w, 33, 47, 252, 266) >= 3, "CE label")
        want(ink_at(data, w, 273, 300, 252, 268) >= 3, "divide glyph")
        want(ink_at(data, w, 273, 300, 450, 466) >= 3, "equals glyph")
        want(not col_at(data, w, 273, 300, 450, 466, ACC, 40, 3),
             "equals key must be plain (no idle accent)")
        want(ink_at(data, w, 114, 126, 300, 315) >= 3, "digit 8 label")
        want(ink_at(data, w, 20, 36, 163, 173, 200) >= 3, "memory row MC")
        want(ink_at(data, w, 175, 196, 163, 173, 200) >= 3, "memory row M+")
        want(ink_at(data, w, 58, 150, 36, 48, 150) >= 4, "mode title")
        want(ink_at(data, w, 288, 306, 35, 51, 150) >= 3, "history icon")

    if sci:
        want(near(px(data, w, 160, 40), WHITE, 8), "sci display white")
        want(col_at(data, w, 14, 42, 136, 148, ACC, 40, 4),
             "DEG accent (active unit)")
        want(ink_at(data, w, 66, 96, 136, 148, 190) >= 3, "HYP label")
        want(ink_at(data, w, 118, 142, 136, 148, 190) >= 3, "F-E label")
        want(near(px(data, w, 160, 157), HAIR, 10), "sci hairline y157")
        want(near(px(data, w, 160, 191), HAIR, 10), "sci hairline y191")
        want(near(px(data, w, 160, 220), WHITE, 8), "sci R1 on white")
        want(near(px(data, w, 160, 300), PAGE, 10), "sci shade")
        want(ink_at(data, w, 150, 172, 204, 216) >= 3, "sci sin label")
        want(ink_at(data, w, 20, 40, 202, 218) >= 3, "sci x^2 glyph")
        want(ink_at(data, w, 86, 108, 243, 259) >= 3, "sci 10^x glyph")
        want(ink_at(data, w, 27, 39, 329, 342, 150) >= 3, "2nd up arrow")
        want(ink_at(data, w, 88, 104, 286, 302) >= 3, "sci CE label")
        want(ink_at(data, w, 273, 300, 449, 466) >= 3, "sci equals glyph")
        want(ink_at(data, w, 20, 36, 172, 183, 200) >= 3, "sci memory row")

    if conv and not sheet_frame:
        want(near(px(data, w, 160, 40), WHITE, 8), "conv page white")
        want(col_at(data, w, 13, 110, 128, 142, ACC, 40, 4),
             "unit1 name accent")
        want(col_at(data, w, 13, 110, 208, 226, ACC, 40, 4),
             "unit2 name accent")
        want(near(px(data, w, 160, 300), PAGE, 10), "conv keypad shade")
        want(ink_at(data, w, 148, 170, 293, 306) >= 3, "conv CE label")
        want(ink_at(data, w, 240, 266, 293, 306, 150) >= 3, "conv back glyph")

    if name == "calc-boot.png":
        want(ink_at(data, w, 270, 308, 96, 130) >= 5, "result zero")
    elif name == "calc-std-result.png":
        want(ink_at(data, w, 260, 308, 96, 130) >= 10, "result 60")
    elif name == "calc-std-sqrt.png":
        want(ink_at(data, w, 270, 308, 96, 130) >= 5, "result 9")
    elif name == "calc-std-percent.png":
        want(ink_at(data, w, 230, 308, 96, 130) >= 10, "result 1,350")
    elif name == "calc-std-invert.png":
        want(ink_at(data, w, 190, 308, 96, 130) >= 10, "result -0.125")
    elif name == "calc-std-memory.png":
        want(ink_at(data, w, 260, 308, 96, 130) >= 5, "result 8 (after MR)")
        want(ink_at(data, w, 20, 36, 163, 173, 200) >= 3,
             "MC enabled (memory present)")
    elif name == "calc-std-history.png":
        want(near(px(data, w, 160, 60), PANE, 12), "history scrim")
        want(near(px(data, w, 160, 200), WHITE, 8), "history panel")
        want(ink_at(data, w, 10, 60, 100, 112) >= 3, "history title")
        want(ink_at(data, w, 230, 308, 146, 200) >= 6, "history entries")
    elif name == "calc-std-memory-sheet.png":
        want(near(px(data, w, 160, 60), PANE, 12), "memory scrim")
        want(near(px(data, w, 160, 200), WHITE, 8), "memory panel")
        want(ink_at(data, w, 10, 80, 100, 112) >= 3, "memory title")
        want(ink_at(data, w, 230, 308, 146, 200) >= 6, "memory values")
    elif name == "calc-navpane.png":
        want(near(px(data, w, 60, 100), PANE, 12), "nav pane bg")
        want(col_at(data, w, 4, 250, 58, 86, (156, 174, 239), 24, 6),
             "selected band #9CAEEF")
        want(near(px(data, w, 300, 100), WHITE, 8),
             "no scrim: page visible right of pane")
        want(ink_at(data, w, 60, 120, 36, 47, 140) >= 3, "pane title")
        want(ink_at(data, w, 48, 100, 64, 76, 150) >= 3, "Standard row")
        want(ink_at(data, w, 48, 110, 120, 137, 150) >= 3, "Programmer row")
    elif name == "calc-sci-boot.png":
        want(ink_at(data, w, 285, 308, 91, 112) >= 5, "sci result zero")
    elif name == "calc-sci-sin30.png":
        want(ink_at(data, w, 250, 308, 88, 112) >= 8, "sin30 result 0.5")
    elif name == "calc-sci-rad-tan.png":
        want(ink_at(data, w, 250, 308, 88, 112) >= 8, "tan(pi/4) ~ 1")
    elif name == "calc-sci-asin.png":
        want(ink_at(data, w, 220, 308, 88, 112) >= 10, "asin(0.5) result")
    elif name == "calc-sci-parens.png":
        want(ink_at(data, w, 250, 308, 88, 112) >= 8, "(2+3)x4 result 20")
    elif name == "calc-sci-factorial.png":
        want(ink_at(data, w, 250, 308, 88, 112) >= 8, "5! result 120")
    elif name == "calc-conv-volume.png":
        want(ink_at(data, w, 13, 60, 93, 110) >= 4, "value1 zero")
        want(ink_at(data, w, 13, 60, 173, 190) >= 4, "value2 zero")
    elif name == "calc-conv-picker.png":
        want(near(px(data, w, 160, 40), PANE, 12), "picker scrim")
        want(near(px(data, w, 160, 200), WHITE, 8), "picker panel")
        want(ink_at(data, w, 20, 120, 116, 134) >= 3, "picker row 0")
        want(ink_at(data, w, 60, 200, 79, 92) >= 3, "picker title")
    elif name == "calc-conv-volume2.png":
        want(ink_at(data, w, 13, 45, 93, 110) >= 4, "typed 2")
        want(ink_at(data, w, 13, 100, 173, 190) >= 8, "0.528... gal")
    elif name == "calc-conv-length.png":
        want(ink_at(data, w, 13, 45, 93, 110) >= 4, "typed 3")
        want(ink_at(data, w, 13, 110, 173, 190) >= 8, "9.84 ft")
    elif name == "calc-conv-temp.png":
        want(ink_at(data, w, 13, 45, 93, 110) >= 4, "typed 100")
        want(ink_at(data, w, 13, 60, 173, 190) >= 8, "212 F")
    elif name == "calc-about.png":
        want(near(px(data, w, 160, 300), WHITE, 8), "about page white")
        want(ink_at(data, w, 12, 116, 64, 83) >= 6, "Calculator title")
        want(col_at(data, w, 12, 100, 181, 195, ACC, 40, 4), "Terms link")
        want(near(px(data, w, 60, 275), (181, 178, 181), 24),
             "feedback button fill")
    if prog and name not in ("calc-prog-bits.png",):
        want(near(px(data, w, 160, 40), WHITE, 8), "prog display white")
        want(near(px(data, w, 160, 260), PAGE, 10) or
             near(px(data, w, 160, 260), WHITE, 8), "prog keypad bands")
        want(near(px(data, w, 60, 230), WHITE, 8), "pivot row white")
        def linehit(y0, y1):
            n = 0
            for yy in range(y0, y1):
                for xx in range(12, 42):
                    c = px(data, w, xx, yy)
                    if c[0] < 200 and c[1] < 200:
                        n += 1
            return n
        want(linehit(129, 140) >= 3, "HEX line label")
        want(linehit(152, 163) >= 3, "DEC line label")
        want(linehit(198, 209) >= 3, "BIN line label")
        want(ink_at(data, w, 20, 36, 36, 48, 150) >= 4, "PROGRAMMER title")
    if prog and name not in ("calc-prog-bits.png",):
        want(ink_at(data, w, 20, 34, 255, 267, 150) >= 3, "Lsh label")
        want(ink_at(data, w, 280, 300, 255, 267, 150) >= 3, "And label")
    if name == "calc-prog-boot.png":
        want(ink_at(data, w, 270, 308, 90, 112) >= 5, "prog result zero")
        want(col_at(data, w, 12, 70, 147, 158, ACC, 40, 4),
             "DEC line active (accent)")
        want(col_at(data, w, 20, 34, 339, 351, (181, 178, 181), 24, 3),
             "A key disabled outside HEX")
        want(ink_at(data, w, 180, 196, 339, 351, 120) >= 3, "8 key enabled")
        want(ink_at(data, w, 130, 150, 380, 392, 150) >= 3, "Mod label")
    elif name == "calc-prog-42.png":
        want(ink_at(data, w, 240, 308, 90, 112) >= 8, "prog result 42")
        want(ink_at(data, w, 42, 80, 129, 140, 150) >= 3, "HEX 2A value")
        want(ink_at(data, w, 42, 110, 198, 209, 150) >= 6, "BIN grouped value")
    elif name == "calc-prog-hex.png":
        want(ink_at(data, w, 250, 308, 90, 112) >= 8, "big value 2A")
        want(col_at(data, w, 12, 70, 129, 140, ACC, 40, 4),
             "HEX line active (accent)")
        want(ink_at(data, w, 20, 34, 339, 351, 150) >= 3, "A key enabled in HEX")
    elif name == "calc-prog-bits.png":
        want(col_at(data, w, 20, 40, 225, 245, ACC, 40, 6),
             "bit dots icon (accent when active)")
        want(col_at(data, w, 245, 300, 437, 479, ACC, 40, 6),
             "bit grid: set bits are blue")
        want(ink_at(data, w, 245, 300, 437, 455, 200) >= 8,
             "bit grid digits present")
    elif name == "calc-prog-bitflip.png":
        want(ink_at(data, w, 250, 308, 90, 112) >= 8, "big value 46")
        want(col_at(data, w, 42, 80, 129, 140, ACC, 40, 3) or
             ink_at(data, w, 42, 80, 129, 140, 150) >= 3, "HEX 2E value")
    elif name == "calc-prog-lsh.png":
        want(ink_at(data, w, 240, 308, 90, 112) >= 8, "5 Lsh 2 = 20")
    elif name == "calc-prog-not.png":
        want(ink_at(data, w, 200, 308, 90, 112) >= 8, "Not 20 = -21")
    elif name == "calc-final.png":
        want(near(px(data, w, 160, 300), WHITE, 8), "final: about page")
        want(ink_at(data, w, 12, 116, 64, 83) >= 6, "final: Calculator title")
        want(col_at(data, w, 12, 100, 181, 195, ACC, 40, 4), "final: Terms link")

    if fails:
        print(f"{name}: FAIL")
        for f in fails:
            print(f"   - {f}")
        return False
    print(f"{name}: OK")
    return True


def main():
    args = sys.argv[1:]
    if not args:
        print("usage: check_calc_shots.py calc-*.png")
        return 2
    ok = all(check(p) for p in args) if len(args) == 1 else \
        all([check(p) for p in args])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
