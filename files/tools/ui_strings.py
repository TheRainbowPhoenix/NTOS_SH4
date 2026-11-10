#!/usr/bin/env python3
"""ui_strings.py — dump every human-readable string from FileExplorer.exe.

Two passes:
  - UTF-16LE strings >= 2 chars (HSTRING literals: menu items, dialog text,
    error messages, automation names)
  - ASCII strings >= 4 (type names, resource keys, ms-appx URIs, file names)

Output (default): files/analysis/FileExplorer_strings.txt
  "0xADDR u16|a  text" sorted by address, with section attribution.
"""
import sys, os, re, struct

try:
    import pefile
except ImportError:
    pefile = None


def sections(data):
    if not pefile:
        return []
    try:
        pe = pefile.PE(data=data, fast_load=True)
        out = []
        for s in pe.sections:
            out.append((s.VirtualAddress + pe.OPTIONAL_HEADER.ImageBase,
                        s.VirtualAddress + pe.OPTIONAL_HEADER.ImageBase +
                        max(s.Misc_VirtualSize, s.SizeOfRawData),
                        s.Name.decode(errors="replace").rstrip("\0")))
        return out
    except Exception:
        return []


def secname(addr, secs):
    for lo, hi, n in secs:
        if lo <= addr < hi:
            return n
    return "?"


def main():
    path = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else \
        os.path.join(os.path.dirname(__file__), "..", "analysis",
                     os.path.splitext(os.path.basename(path))[0] + "_strings.txt")
    data = open(path, "rb").read()
    secs = sections(data)
    base = 0x400000  # default; pe_sections adjusts via file offset mapping

    # file-offset -> VA mapping via pefile
    va_of = {}
    if pefile:
        try:
            pe = pefile.PE(data=data, fast_load=True)
            for s in pe.sections:
                va = pe.OPTIONAL_HEADER.ImageBase + s.VirtualAddress
                raw = s.PointerToRawData
                va_of[(raw, raw + s.SizeOfRawData)] = va
        except Exception:
            pass

    def to_va(off):
        for (lo, hi), va in va_of.items():
            if lo <= off < hi:
                return va + (off - lo)
        return off

    results = []
    # UTF-16LE printable runs, >= 2 chars (short labels like "OK" count)
    for m in re.finditer(rb"(?:[\x20-\x7e]\x00){2,}", data):
        s = m.group().decode("utf-16-le")
        va = to_va(m.start())
        results.append((va, "u16", s))
    # ASCII printable runs >= 4
    for m in re.finditer(rb"[\x20-\x7e]{4,}", data):
        s = m.group().decode("latin1")
        va = to_va(m.start())
        results.append((va, "a", s))
    results.sort()
    with open(out, "w") as f:
        for va, kind, s in results:
            f.write(f"{va:#010x} {secname(va, secs):8s} {kind}  {s}\n")
    print(f"{len(results)} strings -> {out}")


if __name__ == "__main__":
    main()
