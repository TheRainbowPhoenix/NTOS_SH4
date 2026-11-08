#!/usr/bin/env python3
"""Read-only PE32/WinCE SH4 audit (not an ELF-to-PE converter).

Inspects real stock WinCE SH4LE binaries as well as NTOS SH4BE images.
Useful to identify linker ImageBase, .reloc types, large-image UTLB
limits, named dependencies and unsupported CPU endian combinations.
"""
import argparse
import struct
from pathlib import Path


def read16(b, pos):
    if pos < 0 or pos + 2 > len(b):
        raise ValueError("PE 16-bit field outside file")
    return struct.unpack_from("<H", b, pos)[0]


def read32(b, pos):
    if pos < 0 or pos + 4 > len(b):
        raise ValueError("PE 32-bit field outside file")
    return struct.unpack_from("<I", b, pos)[0]


def analyze(path):
    b = Path(path).read_bytes()
    if b[:2] != b"MZ":
        raise ValueError("missing MZ")
    pe = read32(b, 0x3c)
    if pe > len(b) - 24 or b[pe:pe + 4] != b"PE\x00\x00":
        raise ValueError("missing PE signature")
    file_header = pe + 4
    machine = read16(b, file_header)
    nsec = read16(b, file_header + 2)
    optsize = read16(b, file_header + 16)
    opt = file_header + 20
    if read16(b, opt) != 0x10b or optsize < 144:
        raise ValueError("only complete PE32 images supported")
    image_base = read32(b, opt + 28)
    entry = read32(b, opt + 16)
    image_size = read32(b, opt + 56)
    subsystem = read16(b, opt + 68)
    flags = read16(b, file_header + 18)
    sections = []
    sec = opt + optsize
    for i in range(nsec):
        off = sec + i * 40
        if off + 40 > len(b):
            raise ValueError("truncated section header")
        name = b[off:off+8].rstrip(b"\0").decode("latin1")
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", b, off+8)
        sections.append((name, va, max(vsize, rawsize), raw, rawsize))

    def file_offset(rva, length):
        for _, va, vlen, raw, rawsize in sections:
            if va <= rva and rva - va <= rawsize and length <= rawsize - (rva-va):
                off = raw + rva - va
                if off + length <= len(b):
                    return off
        raise ValueError(f"invalid or non-file-backed RVA {rva:#x}")

    def ascii_at(rva):
        off = file_offset(rva, 1)
        end = b.find(b"\0", off, min(off + 260, len(b)))
        if end < 0:
            raise ValueError("unterminated PE name")
        return b[off:end].decode("latin1")

    entries = {}
    reloc_rva, reloc_size = struct.unpack_from("<II", b, opt+136)
    if reloc_rva and reloc_size:
        off = 0
        while off < reloc_size:
            head = file_offset(reloc_rva + off, 8)
            page, block = struct.unpack_from("<II", b, head)
            if block < 8 or block % 4 or block > reloc_size - off or page & 4095:
                raise ValueError(f"invalid relocation block at {off:#x}")
            for k in range(8, block, 2):
                word = read16(b, file_offset(reloc_rva+off+k, 2))
                typ = word >> 12
                entries[typ] = entries.get(typ, 0) + 1
            off += block
    modules = []
    imp_rva, imp_len = struct.unpack_from("<II", b, opt+104)
    if imp_rva and imp_len:
        for idx in range(min(256, imp_len//20)):
            p = file_offset(imp_rva+idx*20, 20)
            oft, _, _, name_rva, iat = struct.unpack_from("<IIIII", b, p)
            if oft == 0 and iat == 0 and name_rva == 0:
                break
            if not name_rva or not iat:
                raise ValueError("invalid import descriptor")
            modules.append(ascii_at(name_rva))
    profile = {
        0x01a6: "SH4 little-endian (stock WinCE; cannot execute on NTOS SH4BE)",
        0x01a7: "NTOS private SH4 big-endian (native linker)",
        0x01a8: "NTOS legacy private SH4 big-endian / Microsoft SH5",
    }.get(machine, "other/unsupported")
    pages = (image_size + 4095) // 4096
    executable = machine in (0x01a7, 0x01a8) and pages <= 55
    return {
        "filename": Path(path).name, "bytes": len(b), "machine": hex(machine),
        "architecture": profile, "image_base": f"0x{image_base:08X}",
        "entry_rva": f"0x{entry:X}", "size_of_image": image_size,
        "utlb_pages": pages, "dll": bool(flags & 0x2000),
        "subsystem": subsystem, "relocations": entries,
        "import_modules": modules,
        "ntos_static_map_candidate": executable,
        "note": ("SH4LE instructions require a separate CPU-endian execution "
                 "environment" if machine == 0x01a6 else
                 "requires demand paging" if pages > 55 else
                 "candidate only: runtime imports/ABI still need validation"),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+", type=Path)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    import json
    reports = []
    for path in args.files:
        try:
            reports.append(analyze(path))
        except (OSError, ValueError, struct.error) as e:
            reports.append({"filename": str(path), "error": str(e)})
    if args.json:
        print(json.dumps(reports, indent=2))
    else:
        for item in reports:
            print(json.dumps(item, sort_keys=True))
    if any("error" in item for item in reports):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
