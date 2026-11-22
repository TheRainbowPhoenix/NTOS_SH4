#!/usr/bin/env python3
"""Build an EX-word-installable flat _CONTENT payload directory.

Every shipped PE file is uppercase DOS 8.3, directly beside ntos_sh.d01.
Long filenames are truncated to readable 8.3 names without touching PE imports;
the Exword resolver applies the same deterministic shortening on lookup.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import struct

REQUIRED = ("HELLO.EXE", "HELLOCORE.EXE", "COREDLL.DLL", "XAMLUI.DLL",
            "VMTEST.EXE", "OBTEST.EXE", "EVTEST.EXE", "POOLTEST.EXE", "IOTEST.EXE", "PROCTEST.EXE", "PROCNEXT.EXE",
            "PROCFINAL.EXE", "MMTEST.EXE", "MMROTEST.EXE", "NATIVEVM.EXE",
            "MMREUSE.EXE", "MMSTRESS.EXE", "ROMDIAG.EXE")
EXTENSIONS = {".EXE", ".DLL", ".CPL"}
def shortname(name: str) -> str:
    """Match exword/shortname.h exactly. Fail rather than truncating paths."""
    if "/" in name or "\\" in name or name.count(".") != 1:
        raise ValueError(f"not an 8.3-convertible file: {name!r}")
    stem, ext = name.rsplit(".", 1)
    stem = stem.upper()
    ext = ext.upper()
    if not (stem and ext in {"EXE", "DLL", "CPL"}):
        raise ValueError(f"invalid PE filename: {name!r}")
    if not all(c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_" for c in stem):
        raise ValueError(f"unsupported PE filename: {name!r}")
    stem = stem[:8]
    return f"{stem}.{ext}"


def appname(relative: Path) -> str:
    """Reserve an A-prefix for Apps so CALC and Apps/calc do not collide.

    Apps/calc/calc.exe -> ACALC.EXE; Apps/powerpoint/powerpoint.exe
    -> APOWERPO.EXE. Reject collisions rather than silently hashing names.
    """
    if relative.suffix.upper() != ".EXE":
        raise ValueError(f"not an app executable: {relative}")
    stem = relative.stem.upper()
    if not stem or any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_" for c in stem):
        raise ValueError(f"invalid app name: {relative}")
    return "A" + stem[:7] + ".EXE"

def sh4_pe(path: Path) -> None:
    with path.open("rb") as f:
        header = f.read(64)
        if len(header) < 64 or header[:2] != b"MZ":
            raise ValueError(f"{path}: missing PE DOS header")
        offset = struct.unpack_from("<I", header, 0x3C)[0]
        if offset > path.stat().st_size - 6:
            raise ValueError(f"{path}: invalid PE offset")
        f.seek(offset)
        if f.read(4) != b"PE\0\0" or f.read(2) != b"\xa8\x01":
            raise ValueError(f"{path}: not SH4BE PE32")


def imported_modules(pe: Path) -> list[str]:
    """Walk PE32 import descriptors using only stdlib (no SH4 toolchain).

    All imports must resolve to flat 8.3 payloads. This catches missing
    dependencies before packaging rather than displaying a vague load error
    on physical Exword hardware.
    """
    data = pe.read_bytes()
    if len(data) < 0x100 or data[:2] != b"MZ":
        return []
    peoff = struct.unpack_from("<I", data, 0x3C)[0]
    if peoff + 24 > len(data):
        return []
    count = struct.unpack_from("<H", data, peoff + 6)[0]
    opt_size = struct.unpack_from("<H", data, peoff + 20)[0]
    opt = peoff + 24
    if opt + opt_size > len(data) or opt_size < 112:
        return []
    if struct.unpack_from("<H", data, opt)[0] != 0x10B:
        return []
    imp, length = struct.unpack_from("<II", data, opt + 104)
    if not imp or not length:
        return []
    sections = []
    for index in range(count):
        start = opt + opt_size + index * 40
        if start + 40 > len(data):
            raise ValueError(f"{pe}: truncated section table")
        vlen, va, rawlen, raw = struct.unpack_from("<IIII", data, start + 8)
        sections.append((va, max(vlen, rawlen), raw))

    def offset(rva: int) -> int:
        for va, span, raw in sections:
            if va <= rva < va + span:
                pos = raw + rva - va
                if pos < len(data):
                    return pos
        raise ValueError(f"{pe}: invalid import RVA 0x{rva:08X}")

    result: list[str] = []
    for index in range(128):
        desc = offset(imp + index * 20)
        if desc + 20 > len(data):
            raise ValueError(f"{pe}: truncated import descriptor")
        vals = struct.unpack_from("<IIIII", data, desc)
        if not any(vals):
            return result
        name_off = offset(vals[3])
        terminator = data.find(b"\0", name_off, min(len(data), name_off + 64))
        if terminator < 0:
            raise ValueError(f"{pe}: unterminated import name")
        result.append(data[name_off:terminator].decode("ascii"))
    raise ValueError(f"{pe}: too many import descriptors")


def products(dist: Path) -> dict[str, Path]:
    dist = dist.resolve()
    if not dist.is_dir():
        raise FileNotFoundError(dist)
    result: dict[str, Path] = {}

    def add(name: str, source: Path) -> None:
        if source.is_symlink() or not source.is_file():
            raise ValueError(f"invalid PE source: {source}")
        if len(name) > 12 or len(name.split(".")[0]) > 8:
            raise ValueError(f"invalid target 8.3 name: {name}")
        sh4_pe(source)
        if name in result:
            raise ValueError(f"8.3 name collision {name}: {result[name]} vs {source}")
        result[name] = source

    for source in sorted(dist.iterdir()):
        if source.is_file() and source.suffix.upper() in EXTENSIONS:
            add(shortname(source.name), source)
    for name in REQUIRED:
        if shortname(name) not in result:
            raise FileNotFoundError(f"missing required PE {name} in {dist}")

    apps = dist / "Apps"
    if apps.is_dir():
        for source in sorted(apps.rglob("*")):
            if source.is_file() and source.suffix.upper() == ".EXE":
                add(appname(source.relative_to(apps)), source)
    # Every transitive module referenced by the actual PE IAT must be
    # uploadable in the same directory under its deterministic 8.3 name.
    for name, source in result.items():
        for dependency in imported_modules(source):
            target = shortname(dependency)
            if target not in result:
                raise FileNotFoundError(
                    f"{name}: missing flat import {dependency} -> {target}"
                )
    return result


def package(dist: Path, folder: Path) -> int:
    mapping = products(dist)
    folder = folder.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    # A reused build tree can contain a previous nested SYSROOT package.
    obsolete = folder / "SYSROOT"
    if obsolete.is_dir():
        shutil.rmtree(obsolete)
    # Purge previous staged EXEs/DLLs/CPLs only; retain .D01 and metadata.
    for source in folder.iterdir():
        if source.is_file() and source.suffix.upper() in EXTENSIONS:
            source.unlink()
    for name, source in sorted(mapping.items()):
        shutil.copyfile(source, folder / name)
    print(f"NTOSH {folder}: {len(mapping)} flat, unique 8.3 WinCE PE files")
    return len(mapping)


def check(dist: Path, folder: Path) -> None:
    mapping = products(dist)
    assert not (folder / "SYSROOT").exists(), "obsolete SYSROOT directory"
    deployed = {p.name for p in folder.iterdir()
                if p.is_file() and p.suffix.upper() in EXTENSIONS}
    assert deployed == set(mapping), (
        f"flat PE mismatch missing={sorted(set(mapping)-deployed)} "
        f"extra={sorted(deployed-set(mapping))}"
    )
    for name, original in mapping.items():
        actual = folder / name
        assert len(name) <= 12 and actual.read_bytes() == original.read_bytes(), (
            f"stale or invalid {name}"
        )
    assert (folder / "ntos_sh.d01").is_file() or not (
        folder / "fileinfo.cji"
    ).exists(), "D01 missing from install folder"
    print(f"PASS {folder}: {len(mapping)} flat SH4BE 8.3 PEs, no subfolders")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--from-dist", type=Path, required=True)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.check:
        check(args.from_dist, args.package)
    else:
        package(args.from_dist, args.package)
