#!/usr/bin/env python3
"""Verify SH4BE COREDLL GDI exports target real functions, not silent stubs.

The COREDLL PE producer intentionally publishes ~1900 names with one stub
RVA for unimplemented WinCE calls. A successful import snap ALONE is not
proof of an implemented function. Audit actual PE32 exports and the linked
GDIBLIT import names to prevent a false-green compatibility test.
"""
from __future__ import annotations
import struct
import sys
from pathlib import Path

GDI = {
    "GetDC", "ReleaseDC", "CreateCompatibleDC", "DeleteDC",
    "CreateCompatibleBitmap", "SelectObject", "DeleteObject",
    "SetPixel", "GetPixel", "CreateSolidBrush", "FillRect", "BitBlt",
}


class PE:
    def __init__(self, filename: str):
        self.data = Path(filename).read_bytes()
        d = self.data
        if d[:2] != b"MZ":
            raise ValueError(f"{filename}: missing MZ")
        nt = struct.unpack_from("<I", d, 0x3C)[0]
        if d[nt:nt + 4] != b"PE\x00\x00":
            raise ValueError(f"{filename}: missing PE")
        machine, section_count = struct.unpack_from("<HH", d, nt + 4)
        if machine != 0x01A8:  # NTOS-approved SH4 big endian
            raise ValueError(f"{filename}: wrong machine 0x{machine:04x}")
        optional_size = struct.unpack_from("<H", d, nt + 20)[0]
        optional = nt + 24
        if struct.unpack_from("<H", d, optional)[0] != 0x10B:
            raise ValueError(f"{filename}: not PE32")
        self.directories = [
            struct.unpack_from("<II", d, optional + 96 + i * 8)
            for i in range(2)
        ]
        self.sections = []
        start = optional + optional_size
        for i in range(section_count):
            s = start + i * 40
            virtual_size, virtual_rva, raw_size, raw_offset = struct.unpack_from(
                "<IIII", d, s + 8
            )
            self.sections.append(
                (virtual_rva, max(virtual_size, raw_size), raw_offset, raw_size)
            )

    def off(self, rva: int) -> int:
        for start, size, raw, raw_size in self.sections:
            if start <= rva < start + size and rva - start < raw_size:
                return raw + rva - start
        raise ValueError(f"RVA 0x{rva:08x} is not backed by PE bytes")

    def u16(self, at: int) -> int:
        return struct.unpack_from("<H", self.data, at)[0]

    def u32(self, at: int) -> int:
        return struct.unpack_from("<I", self.data, at)[0]

    def cstr(self, rva: int) -> str:
        start = self.off(rva)
        end = self.data.find(b"\0", start, min(len(self.data), start + 128))
        if end < 0:
            raise ValueError(f"nonterminated PE string at RVA {rva:08x}")
        return self.data[start:end].decode("ascii")

    def exports(self) -> dict[str, int]:
        rva, size = self.directories[0]
        if not rva or not size:
            raise ValueError("missing export directory")
        start = self.off(rva)
        _, _, _, _, _, _, nfunc, nnames, eat, npt, opt = struct.unpack_from(
            "<IIHHIIIIIII", self.data, start
        )
        if not (1 <= nnames <= nfunc <= 4096):
            raise ValueError("unreasonable export directory")
        result = {}
        for i in range(nnames):
            name_rva = self.u32(self.off(npt + i * 4))
            name = self.cstr(name_rva)
            ordinal = self.u16(self.off(opt + i * 2))
            if ordinal >= nfunc or name in result:
                raise ValueError("bad PE export ordinal/name")
            result[name] = self.u32(self.off(eat + 4 * ordinal))
        return result

    def imports(self) -> dict[str, set[str]]:
        rva, size = self.directories[1]
        if not rva or not size:
            raise ValueError("missing import directory")
        result: dict[str, set[str]] = {}
        for i in range(32):
            off = self.off(rva + i * 20)
            oft, _, _, name, first = struct.unpack_from("<IIIII", self.data, off)
            if not (oft or name or first):
                break
            dll = self.cstr(name).upper()
            names: set[str] = set()
            thunk = oft or first
            for j in range(128):
                value = self.u32(self.off(thunk + j * 4))
                if not value:
                    break
                if value & 0x80000000:
                    raise ValueError("unexpected ordinal import, require named GDI")
                names.add(self.cstr(value + 2))
            result[dll] = names
        return result


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("Usage: check_gdi_import_exports.py COREDLL.DLL GDIBLIT.EXE")
    dll, exe = PE(sys.argv[1]), PE(sys.argv[2])
    exports = dll.exports()
    stub = exports.get("AbortDoc")
    if stub is None or not stub:
        raise ValueError("cannot find known COREDLL fallback stub AbortDoc")
    missing = GDI - exports.keys()
    fake = sorted(name for name in GDI if exports.get(name) == stub)
    if missing or fake or any(not exports.get(name) for name in GDI):
        raise ValueError(f"GDI absent {sorted(missing)}, incorrectly stubbed {fake}")
    imports = exe.imports().get("COREDLL.DLL", set())
    if imports != GDI:
        raise ValueError(
            f"GDIBLIT IAT mismatch: missing {sorted(GDI-imports)}, "
            f"extra {sorted(imports-GDI)}"
        )
    print(
        "PASS SH4BE COREDLL GDI PE: 12 named imports, 12 real "
        "non-stub export RVAs and correct machine/PE32"
    )


if __name__ == "__main__":
    main()
