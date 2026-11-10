#!/usr/bin/env python3
"""pri_dump_all.py — dump every UTF-16 string from every section of a
mrm_pri2 resources.pri, with section attribution.

Based on edge/tools/pri_dump.py (same header/directory reversal) but walks
*all* rm_dataitem sections (the Edge tool only dumped the first one), so the
en-US language blob (display strings) and every XAML metadata heap land in
one annotated file.

Usage:
  python3 pri_dump_all.py <resources.pri> [--out <dir>]

Writes:
  <out>/pri_all_strings.txt      every string, "0xADDR [section] text"
  <out>/pri_sections.txt         section directory listing
"""
import sys, os, re, struct

MAGIC = b"mrm_pri2"


def parse_header(data):
    assert data[:8] == MAGIC, "not a mrm_pri2 file"
    version, filesize, flags, hdrlen, checksum = struct.unpack_from("<5I", data, 8)
    return version, filesize, flags, hdrlen, checksum


def parse_dir(data, hdrlen):
    entries = []
    off = 0x20
    while off + 32 <= hdrlen:
        name = data[off:off + 16].rstrip(b"\0 ").decode(errors="replace")
        unk = struct.unpack_from("<Q", data, off + 16)[0]
        sec_off, sec_size = struct.unpack_from("<II", data, off + 24)
        entries.append((name, unk, sec_off, sec_size))
        off += 32
    return entries


def carve_strings(blob, base):
    """extract UTF-16LE printable strings >= 4 chars"""
    for m in re.finditer(rb"(?:[\x20-\x7e]\x00){4,}", blob):
        s = m.group().decode("utf-16-le", errors="replace")
        yield base + m.start(), s


def main():
    path = sys.argv[1]
    args = sys.argv[2:]
    outdir = os.path.join(os.path.dirname(__file__), "..", "analysis", "pri_out")
    if "--out" in args:
        outdir = args[args.index("--out") + 1]
    os.makedirs(outdir, exist_ok=True)
    data = open(path, "rb").read()
    version, filesize, flags, hdrlen, checksum = parse_header(data)
    entries = parse_dir(data, hdrlen)

    with open(os.path.join(outdir, "pri_sections.txt"), "w") as f:
        f.write(f"magic=mrm_pri2 version={version:#x} filesize={filesize} "
                f"flags={flags:#x} hdrlen={hdrlen:#x} checksum={checksum:#x}\n")
        for i, (name, unk, off, size) in enumerate(entries):
            f.write(f"[{i:02d}] {name} @{off:#x} size={size:#x}\n")

    n = 0
    with open(os.path.join(outdir, "pri_all_strings.txt"), "w") as f:
        for i, (name, unk, off, size) in enumerate(entries):
            blob = data[off:off + size]
            seen = set()
            for addr, s in carve_strings(blob, off):
                if s in seen:
                    continue
                seen.add(s)
                f.write(f"{addr:#010x} [{i:02d}:{name}] {s}\n")
                n += 1
    print(f"{len(entries)} sections, {n} unique strings -> {outdir}/pri_all_strings.txt")


if __name__ == "__main__":
    main()
