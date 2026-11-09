#!/usr/bin/env python3
"""pe_scan.py — quick PE triage for the Edge x86 UWP binaries.

Dumps per-file: machine, subsystem, timestamp, sections, imports (with
delay-load flagged), exports, and WinRT/interesting strings. Pure pefile.
Output: edge/analysis/pe_scan/<name>.txt
"""
import sys, os, json, re
from datetime import datetime, timezone

import pefile

INTERESTING_STRING_RE = re.compile(
    r"(xaml|spartan|edgehtml|chakra|browser|viewmodel|application|"
    r"windows\.|microsoft\.|webview|phone|hub|tabs?|address|favorites|"
    r"history|reading|findonpage|pdf|small.?screen)",
    re.IGNORECASE)


def rva_str(rva, pe):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return f"{rva:#010x} ({s.Name.decode(errors='replace').rstrip(chr(0))})"
    return f"{rva:#010x}"


def scan(path, outdir):
    name = os.path.basename(path)
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
        pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT'],
        pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT'],
        pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE'],
        pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_TLS'],
    ])
    lines = []
    w = lines.append
    w(f"=== {name} ({os.path.getsize(path):,} bytes) ===")
    mach = pefile.MACHINE_TYPE.get(pe.FILE_HEADER.Machine, hex(pe.FILE_HEADER.Machine))
    sub = pefile.SUBSYSTEM_TYPE.get(pe.OPTIONAL_HEADER.Subsystem, "?")
    ts = pe.FILE_HEADER.TimeDateStamp
    when = datetime.fromtimestamp(ts, tz=timezone.utc).isoformat() if ts else "0 (repro build?)"
    dllchar = pe.OPTIONAL_HEADER.DllCharacteristics
    w(f"machine={mach} subsystem={sub} timestamp={ts} ({when})")
    w(f"bits={'64' if pe.OPTIONAL_HEADER.Magic==0x20b else '32'} "
      f"entry={pe.OPTIONAL_HEADER.AddressOfEntryPoint:#x} base={pe.OPTIONAL_HEADER.ImageBase:#x}")
    w(f"dllchars={dllchar:#06x}"
      f"{' NX' if dllchar & 0x100 else ''}"
      f"{' DYNAMIC_BASE' if dllchar & 0x40 else ''}"
      f"{' NO_SEH' if dllchar & 0x400 else ''}"
      f"{' TERMINAL_SERVER_AWARE' if dllchar & 0x8000 else ''}")
    w(f"sections: " + " ".join(
        s.Name.decode(errors='replace').rstrip('\0') for s in pe.sections))

    # ---- imports
    w("\n--- IMPORTS ---")
    total = 0
    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll = entry.dll.decode(errors='replace')
            funcs = []
            for imp in entry.imports:
                if imp.name:
                    funcs.append(imp.name.decode(errors='replace'))
                else:
                    funcs.append(f"ord{imp.ordinal}")
            total += len(funcs)
            w(f"{dll}: {len(funcs)} functions")
            # show all for small lists, trim for huge
            if len(funcs) <= 40:
                for f in funcs:
                    w(f"    {f}")
            else:
                for f in funcs[:15]:
                    w(f"    {f}")
                w(f"    ... ({len(funcs)-15} more)")
    # ---- delay imports
    if hasattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT"):
        w("\n--- DELAY IMPORTS ---")
        for entry in pe.DIRECTORY_ENTRY_DELAY_IMPORT:
            dll = entry.dll.decode(errors='replace')
            funcs = [imp.name.decode(errors='replace') if imp.name else f"ord{imp.ordinal}"
                     for imp in entry.imports]
            total += len(funcs)
            w(f"{dll} (DELAY): {len(funcs)} functions")
            if len(funcs) <= 40:
                for f in funcs:
                    w(f"    {f}")
            else:
                for f in funcs[:15]:
                    w(f"    {f}")
                w(f"    ... ({len(funcs)-15} more)")
    if total == 0:
        w("(no imports — SxS/WinRT only?)")

    # ---- exports
    w("\n--- EXPORTS ---")
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        symbols = [(e.name.decode(errors='replace') if e.name else f"ord{e.ordinal}", e.ordinal)
                   for e in pe.DIRECTORY_ENTRY_EXPORT.symbols]
        w(f"{len(symbols)} exported symbols:")
        for s, o in symbols[:200]:
            w(f"    {o:4d} {s}")
        if len(symbols) > 200:
            w(f"    ... ({len(symbols)-200} more)")
    else:
        w("(none)")

    pe.close()
    out = os.path.join(outdir, name + ".txt")
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    return out


def main():
    args = sys.argv[1:]
    outdir = args.pop() if args and args[-1].startswith("--out=") else None
    if outdir is None:
        outdir = os.path.join(os.path.dirname(__file__), "..", "analysis", "pe_scan")
    outdir = outdir.removeprefix("--out=")
    os.makedirs(outdir, exist_ok=True)
    for p in args:
        print(scan(p, outdir))


if __name__ == "__main__":
    main()
