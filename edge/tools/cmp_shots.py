#!/usr/bin/env python3
"""cmp_shots.py — compare our CI screenshots against the real-device reference.

Metric (per user spec): split both images into 16x16 blocks, average each
block's colour; a block "differs" when any channel differs by more than TOL.
Report per-image: % blocks differing, plus the worst offending blocks (for
layout debugging).
"""
import sys, os
from PIL import Image

BLK = 16
TOL = 32  # per-channel tolerance on block-average colour

def blocks(im):
    px = im.load()
    W, H = im.size
    out = {}
    for by in range(0, H, BLK):
        for bx in range(0, W, BLK):
            rs = gs = bs = n = 0
            for y in range(by, min(by + BLK, H)):
                for x in range(bx, min(bx + BLK, W)):
                    r, g, b = px[x, y][:3]
                    rs += r; gs += g; bs += b; n += 1
            out[(bx, by)] = (rs // n, gs // n, bs // n)
    return out

def compare(ours, ref, tol=TOL):
    a = Image.open(ours).convert("RGB")
    b = Image.open(ref).convert("RGB")
    if a.size != b.size:
        print(f"  SIZE MISMATCH {a.size} vs {b.size}")
        return
    ba, bb = blocks(a), blocks(b)
    bad = []
    for k in sorted(ba):
        ca, cb = ba[k], bb[k]
        if (abs(ca[0]-cb[0]) > tol or abs(ca[1]-cb[1]) > tol or
                abs(ca[2]-cb[2]) > tol):
            bad.append((k, ca, cb))
    pct = 100.0 * len(bad) / len(ba)
    print(f"{os.path.basename(ours)} vs {os.path.basename(ref)}: "
          f"{len(bad)}/{len(ba)} blocks differ = {pct:.1f}%")
    # worst 14 blocks sorted by max channel delta
    bad.sort(key=lambda t: -max(abs(t[1][i]-t[2][i]) for i in range(3)))
    for (k, ca, cb) in bad[:14]:
        print(f"    block@({k[0]:3d},{k[1]:3d}) ours=rgb{ca} ref=rgb{cb}")
    return pct

if __name__ == "__main__":
    pairs = [
        ("/tmp/ci-boot.png",        "home.png"),
        ("/tmp/ci-menu.png",        "context.png"),
        ("/tmp/ci-tabs.png",        "tabs.png"),
        ("/tmp/ci-clear.png",       "clean_hist.png"),
    ]
    ref_dir = "/tmp/refscreens"
    for ours, ref in pairs:
        if os.path.exists(ours) and os.path.exists(os.path.join(ref_dir, ref)):
            compare(ours, os.path.join(ref_dir, ref))
