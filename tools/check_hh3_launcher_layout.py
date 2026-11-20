#!/usr/bin/env python3
"""Check EXACT PT_LOAD restrictions of Hollyhock-Custom-Launcher HH3 ELFLoader.

The launcher returns 1 on std::exception; in particular it raises
"ELF segment overlaps reserved RAM" if a PT_LOAD crosses 0x8D000000.
A code+data ELF with valid entry is not enough: ALL load segments must
satisfy app_range() in the user's actual launcher.

No third-party Python packages. Runs on stripped NTOS.hh3 AFTER linking.
"""
import argparse
import struct
from pathlib import Path

APP_START = 0x8C000000
RESERVED_START = 0x8CFD0000
RESERVED_END = 0x8CFF0000
APP_END = 0x8D000000
PT_LOAD = 1
PF_X, PF_W, PF_R = 1, 2, 4

def check(data, headroom):
    if len(data) < 52 or data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
        raise ValueError("not a big-endian ELF32 HH3")
    (_, e_type, e_machine, version, entry, phoff, _shoff, _flags,
     ehsize, phentsize, phnum, _shentsize, _shnum, _shstrndx) = (
         struct.unpack_from(">16sHHIIIIIHHHHHH", data, 0))
    if e_type not in (2, 4) or e_machine != 42 or version != 1:
        raise ValueError("not a static SuperH executable")
    if phentsize != 32 or phnum < 1 or phoff + phentsize * phnum > len(data):
        raise ValueError("malformed ELF program header table")
    max_end = APP_START
    has_entry = False
    count = 0
    for i in range(phnum):
        p_type, p_offset, va, _pa, file_len, mem_len, flags, _align = (
            struct.unpack_from(">IIIIIIII", data, phoff + i * phentsize))
        if p_type != PT_LOAD:
            continue
        if file_len > mem_len or flags & ~(PF_X | PF_W | PF_R):
            raise ValueError("illegal ELF segment sizes/flags")
        if not mem_len or not flags:
            continue
        count += 1
        if (va < APP_START or va >= APP_END or mem_len > APP_END - va or
                not (va + mem_len <= RESERVED_START or va >= RESERVED_END)):
            raise ValueError(
                f"ELF segment overlaps reserved RAM: "
                f"PT_LOAD[{i}] VA=0x{va:08X} end=0x{va+mem_len:08X} "
                f"(HH3 launcher limit 0x{APP_END:08X})")
        if file_len and (p_offset > len(data) or
                         file_len > len(data) - p_offset):
            raise ValueError("ELF segment outside file")
        if flags & PF_X and va <= entry < va + mem_len:
            has_entry = True
        max_end = max(max_end, va + mem_len)
    if count == 0 or not has_entry or entry & 1:
        raise ValueError("ELF entry point outside executable RAM")
    available = APP_END - max_end
    if available < headroom:
        raise ValueError(
            f"HH3 has only {available} bytes free before 0x{APP_END:08X}; "
            f"require >= {headroom} to avoid next startup regression")
    return count, available, max_end

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+")
    parser.add_argument("--min-headroom", type=int, default=4096)
    args = parser.parse_args()
    for file in args.files:
        try:
            n, margin, last = check(Path(file).read_bytes(), args.min_headroom)
        except ValueError as e:
            parser.exit(1, f"FAIL {file}: {e}\n")
        print(f"PASS HH3 custom launcher: {file}, {n} load segments, "
              f"RAM end=0x{last:08X}, headroom={margin} bytes")

if __name__ == "__main__":
    main()
