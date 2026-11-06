#!/usr/bin/env python3
"""Inspect the raw SH4BE COFF/PE32 .NTX format expected by NTOS.

Usage: python3 tools/check_ntx.py dist/HELLO.NTX
This is a *raw COFF* image (no DOS MZ or PE signature). All multibyte
fields are big-endian, and the COFF file header is 20 bytes.
"""
import argparse
from pathlib import Path
import struct
import sys


def u16(data, offset):
    return struct.unpack_from(">H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def inspect(path):
    data = Path(path).read_bytes()
    if len(data) < 20:
        raise ValueError("truncated 20-byte COFF header")
    machine, count = struct.unpack_from(">HH", data)
    optional_size = u16(data, 16)
    flags = u16(data, 18)
    print(f"{path}: {len(data)} bytes, machine=0x{machine:04X}, "
          f"sections={count}, optional={optional_size}, flags=0x{flags:04X}")
    if machine != 0x01A8:
        raise ValueError("not a raw SH4 big-endian COFF (.NTX)")
    if count < 1 or count > 8:
        raise ValueError("section count outside NTOS's 1..8 limit")
    opt = 20
    section_base = opt + optional_size
    if optional_size < 32 or section_base + count * 40 > len(data):
        raise ValueError("truncated PE32 optional header / section headers")
    if u16(data, opt) != 0x010B:
        raise ValueError("not a PE32 optional header")
    base = u32(data, opt + 28)
    size = u32(data, opt + 56) if optional_size >= 60 else 0
    entry = u32(data, opt + 16)
    num_dirs = u32(data, opt + 92) if optional_size >= 96 else 0
    reloc_rva = u32(data, opt + 136) if optional_size >= 144 else 0
    reloc_size = u32(data, opt + 140) if optional_size >= 144 else 0
    print(f"  preferred base=0x{base:08X}, entry RVA=0x{entry:X}, "
          f"image size={size}, PE directories={num_dirs}")
    print(f"  relocation directory RVA=0x{reloc_rva:X}, size={reloc_size}")
    if num_dirs < 6 and reloc_size:
        print("  WARNING: legacy/broken PE32 NumberOfRvaAndSizes ("
              "regenerate this NTX with the fixed elf2ntx.py)")
    sections = []
    for i in range(count):
        h = section_base + i * 40
        name = data[h:h + 8].split(bytes((0,)), 1)[0].decode("ascii", "replace")
        vsize, rva, raw_size, raw_offset, reloc_ptr = struct.unpack_from(
            ">IIIII", data, h + 8)
        relocs = u16(data, h + 32)
        sections.append((name, vsize, rva, raw_size, raw_offset, reloc_ptr, relocs))
        print(f"  {name:8s} RVA=0x{rva:X} vsize={vsize} "
              f"raw={raw_size}@0x{raw_offset:X}, COFF relocs={relocs}")
    has_obj = False
    sym_off, sym_n = u32(data, 8), u32(data, 12)
    if not (flags & 1) and sym_off and sym_n and sym_off + sym_n * 18 <= len(data):
        for _, _, _, _, _, ptr, n in sections:
            if n and ptr and ptr + n * 10 <= len(data):
                has_obj = True
    has_base = bool(reloc_rva and reloc_size >= 8)
    num_fixups = 0
    if has_base:
        mapped = False
        for name, vsize, rva, raw_size, raw_off, _, _ in sections:
            if rva <= reloc_rva and reloc_rva - rva + reloc_size <= raw_size:
                start = raw_off + reloc_rva - rva
                if start + reloc_size > len(data):
                    raise ValueError(".reloc data extends past end of file")
                mapped = True
                off, end = start, start + reloc_size
                while off + 8 <= end:
                    page, block = struct.unpack_from(">II", data, off)
                    if block < 8 or block % 2 or off + block > end:
                        raise ValueError("invalid .reloc block")
                    for index in range((block - 8) // 2):
                        word = u16(data, off + 8 + 2 * index)
                        kind = word >> 12
                        if kind == 3:
                            num_fixups += 1
                        elif kind != 0:
                            raise ValueError(f"unsupported base relocation type {kind}")
                    off += block
                if off != end:
                    raise ValueError("trailing incomplete .reloc record")
                break
        if not mapped:
            raise ValueError(".reloc directory is not backed by raw section bytes")
    if not (has_obj or has_base):
        raise ValueError("NO usable relocations: fix the link/pack step, "
                         "do not just clear IMAGE_FILE_RELOCS_STRIPPED")
    print(f"  OK: base relocations={has_base} (HIGHLOW fixes={num_fixups}), "
          f"COFF object relocations={has_obj}")
    if has_base and num_fixups == 0:
        print("  NOTE: empty .reloc is valid only if the image has no "
              "absolute addresses needing rebasing")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ntx", help="raw SH4BE COFF executable")
    args = parser.parse_args()
    try:
        inspect(args.ntx)
    except (OSError, ValueError, struct.error) as error:
        print(f"INVALID NTX: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
