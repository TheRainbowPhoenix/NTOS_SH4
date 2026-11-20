#!/usr/bin/env python3
"""Validate actual SH4BE PE section protections at 4096-byte UTLB granularity.

Unlike source-only host tests this parses the .EXE files emitted by
elf2ntx.py. The target RO section must not share ANY of its occupied 4KiB
pages with writable sections; the pager correctly unions permissions.
"""
import struct
import sys
from pathlib import Path

WRITE = 0x80000000
READ = 0x40000000
PAGE = 4096


def sections(path):
    data = Path(path).read_bytes()
    if data[:2] != b"MZ":
        raise AssertionError(f"{path}: no MZ")
    at = struct.unpack_from("<I", data, 0x3C)[0]
    if data[at:at+4] != b"PE\0\0":
        raise AssertionError(f"{path}: no PE")
    count = struct.unpack_from("<H", data, at+6)[0]
    optional = struct.unpack_from("<H", data, at+20)[0]
    off = at+24+optional
    result = []
    for i in range(count):
        p = off+40*i
        name = data[p:p+8].rstrip(b"\0").decode("ascii")
        vsize, rva, raw = struct.unpack_from("<III", data, p+8)
        rights = struct.unpack_from("<I", data, p+36)[0]
        result.append((name, rva, max(vsize, raw), rights))
    return result


def check(path):
    secs = sections(path)
    ro = [s for s in secs if s[0] == ".rodata"]
    if not ro:
        raise AssertionError(f"{path}: .rodata absent")
    for name, rva, size, flags in ro:
        if not (flags & READ) or flags & WRITE:
            raise AssertionError(f"{path}: {name} PE incorrectly marked writable")
        first, last = rva // PAGE, (rva+size-1)//PAGE
        for page in range(first, last+1):
            start, end = page*PAGE, (page+1)*PAGE
            for other, other_rva, other_size, other_flags in secs:
                if (other_flags & WRITE and other_rva < end
                        and other_rva+other_size > start):
                    raise AssertionError(
                        f"{path}: RO page {page} overlaps writable {other}"
                    )
    if Path(path).name.upper() == "MMROTEST.EXE":
        if not any(s[0] == ".bss" and s[3] & WRITE for s in secs):
            raise AssertionError("MMROTEST: .bss must remain writable")
    print(f"PASS PE RO physical 4KiB page boundaries: {path}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        check(p)
