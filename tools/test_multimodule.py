#!/usr/bin/env python3
"""Validate real PE multi-module import descriptors in built NTOS payloads.

Runs with Python stdlib only. Checks named DLL dependencies and image bases.
Does not replace hardware UTLB/linker execution testing.
"""
from pathlib import Path
import struct


def u16(data, pos):
    return struct.unpack_from("<H", data, pos)[0]


def u32(data, pos):
    return struct.unpack_from("<I", data, pos)[0]


def inspect(path):
    data = Path(path).read_bytes()
    assert data[:2] == b"MZ", path
    pe = u32(data, 0x3C)
    assert data[pe:pe + 4] == b"PE\0\0", path
    coff = pe + 4
    nsections = u16(data, coff + 2)
    opt = coff + 20
    optsize = u16(data, coff + 16)
    assert u16(data, opt) == 0x10B
    base = u32(data, opt + 28)
    table = opt + optsize
    sections = []
    for i in range(nsections):
        p = table + i * 40
        sections.append((u32(data, p + 12), u32(data, p + 8),
                         u32(data, p + 20), u32(data, p + 16)))
    def offset(rva):
        for va, virtual_size, raw, raw_size in sections:
            if va <= rva < va + max(virtual_size, raw_size):
                d = rva - va
                assert d < raw_size, (path, hex(rva))
                return raw + d
        if rva < u32(data, opt + 60):
            return rva
        raise AssertionError((path, "invalid RVA", hex(rva)))
    def cstr(rva):
        i = offset(rva)
        j = data.index(b"\0", i)
        return data[i:j].decode("ascii")
    imp_rva = u32(data, opt + 96 + 8)
    modules = []
    if imp_rva:
        for i in range(8):
            p = offset(imp_rva + i * 20)
            oft, timestamp, fwd, name, ft = struct.unpack_from("<IIIII", data, p)
            if not (oft or name or ft):
                break
            assert oft and name and ft
            funcs = []
            for j in range(128):
                entry = u32(data, offset(oft + j * 4))
                if entry == 0:
                    break
                assert not (entry & 0x80000000), "unexpected ordinal"
                funcs.append(cstr(entry + 2))
            assert funcs, (path, "empty module", name)
            modules.append((cstr(name), funcs))
        else:
            raise AssertionError("missing PE descriptor terminator")
    return base, modules


def image_info(path):
    data = Path(path).read_bytes()
    optional = u32(data, 0x3C) + 24
    base = u32(data, optional + 28)
    size = u32(data, optional + 56)
    return base, (size + 4095) // 4096


def choose_base(preferred, pages, used):
    """Independent host model of NTOS's 32-set/four-way first-fit logic."""
    def fits(base):
        if base < 0x00100000 or base >= 0x07FF0000 or base & 4095:
            return False
        if base + pages * 4096 > 0x07FF0000:
            return False
        occupancy = [0] * 32
        for vbase, count in [(0x08100000, 4)] + list(used):
            if base < vbase + count*4096 and vbase < base+pages*4096:
                return False
            for page in range(count):
                i = ((vbase >> 12) + page) & 31
                occupancy[i] += 1
                if occupancy[i] > 4:
                    return False
        for page in range(pages):
            i = ((base >> 12)+page)&31
            occupancy[i] += 1
            if occupancy[i] > 4:
                return False
        return True

    if fits(preferred):
        return preferred
    for va in range(0x01000000, 0x07FF0000, 4096):
        if fits(va):
            return va
    raise AssertionError("no four-way compatible UTLB address")


def test_utlb_sets():
    files = ("dist/METROWIN.EXE", "dist/COREDLL.DLL",
             "dist/XAMLUI.DLL")
    images = [image_info(path) for path in files]
    assert images[1][0] == images[2][0] == 0x10000000
    used = []
    for preferred, pages in images:
        base = choose_base(preferred, pages, used)
        used.append((base, pages))
        print(f"UTLB: preferred {preferred:#010x}, assigned {base:#010x}, "
              f"pages={pages}")
    assert used[0][0] == 0x00400000
    assert used[1][0] != used[2][0]
    assert used[1][0] != 0x10000000 and used[2][0] != 0x10000000
    print("PASS UTLB: shared 0x10000000 DLL ImageBase relocates to "
          "distinct, four-way-compatible mappings")


def main():
    metro_base, metro = inspect("dist/METROWIN.EXE")
    xaml_base, xaml = inspect("dist/XAMLUI.DLL")
    core_base, core = inspect("dist/COREDLL.DLL")
    assert metro_base == 0x00400000
    assert core_base == xaml_base == 0x10000000
    assert [name for name, _ in metro] == ["COREDLL.DLL", "XAMLUI.DLL"], metro
    assert [name for name, _ in xaml] == ["COREDLL.DLL"], xaml
    assert not core, core
    assert {"xamlui_tile", "xamlui_label", "xamlui_switch"}.issubset(
        set(metro[1][1]))
    assert {"CreateSolidBrush", "FillRect", "TextOutW"}.issubset(
        set(xaml[0][1]))
    test_utlb_sets()
    for name, funcs in metro + xaml:
        print(f"PASS {name}: {len(funcs)} IAT import(s)")
    print("PASS PE multi-DLL import tables; hardware execution still needs testing")


if __name__ == "__main__":
    main()
