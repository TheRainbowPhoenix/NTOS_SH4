#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Artem Novak
# SPDX-License-Identifier: BSD-3-Clause
"""Separate 1.5 MiB diagnostic file, never linked into the PE executable."""
import sys
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit("usage: make_romprobe.py OUTPUT")
output = Path(sys.argv[1])
output.parent.mkdir(parents=True, exist_ok=True)
with output.open("wb") as out:
    out.truncate(0x180000)
    for offset, byte in ((0, 0x5a), (0x080000, 0xa5), (0x17ffff, 0x3c)):
        out.seek(offset)
        out.write(bytes([byte]))
assert output.stat().st_size == 0x180000
