#!/usr/bin/env python3
"""wpar.py — cross-host screenshot parity gate.

Compares two screenshot sets produced by the SAME e2e script on the two
hosts (ntos_linux vs ntos_windows under wine). The payload, framebuffer,
PNG writer and script engine are shared, so every frame must be
byte-identical; any difference means a host behavioural fork.

usage: wpar.py <linux-prefix> <windows-prefix> [frame ...]
       (no frame list -> compare every <linux-prefix>-*.png found)
exit 0 = all identical, exit 1 = any frame differs/missing
"""
import re
import sys
from pathlib import Path


def frames_for(prefix: str) -> list:
    pat = re.compile(re.escape(prefix) + r"-([a-z0-9-]+)\.png$")
    out = []
    for p in Path(".").glob(prefix + "-*.png"):
        m = pat.match(p.name)
        if m:
            out.append(m.group(1))
    return sorted(set(out))


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    a, b = sys.argv[1], sys.argv[2]
    want = sys.argv[3:] or frames_for(a)
    if not want:
        print(f"wpar: no {a}-*.png frames found")
        return 1
    bad = 0
    for f in want:
        pa, pb = Path(f"{a}-{f}.png"), Path(f"{b}-{f}.png")
        if not pa.is_file() or not pb.is_file():
            print(f"MISSING: {f} ({pa.exists()}, {pb.exists()})")
            bad += 1
            continue
        da, db = pa.read_bytes(), pb.read_bytes()
        if da == db:
            print(f"IDENTICAL: {f} ({len(da)} bytes)")
        else:
            print(f"DIFFERS:   {f} ({len(da)} vs {len(db)} bytes)")
            bad += 1
    if bad:
        print(f"wpar: {bad} frame(s) not identical — host fork!")
        return 1
    print(f"wpar: all {len(want)} frames byte-identical across hosts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
