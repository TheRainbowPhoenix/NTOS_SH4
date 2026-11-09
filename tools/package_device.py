#!/usr/bin/env python3
"""Package exactly the files to copy onto the CP400.

The build tree contains ELF/maps, tester EXEs and experimental DLLs. This
copies only user-facing apps, their real recursive PE dependencies, and the
file-backed applet/assets tree into one flat installable directory.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil

import pefile

APPS = (
    "STARTHOST.EXE", "CALC.EXE", "CONTROL.EXE", "METROWIN.EXE",
    "MEMTOOL.EXE", "SVFEDITOR.EXE", "HELLO.EXE", "HELLOCORE.EXE",
    "PAINT.EXE", "WINLAB.EXE", "GUIWIN.EXE",
)
EXTRA_MODULES = ("DEMO.CPL",)
SIDE_CARS = ("home.dat", "apps.dat", "nav.dat")
README = """NTOS for Casio fx-CP400 — copy this folder's CONTENTS to storage.
Keep Apps/ and Windows/System32/ directory structure. Install NTOS.hh3 as
the calculator add-in. COREDLL.DLL is shared by ALL phone and native
WinCE-style programs; do not replace it with an experimental CE DLL.

If updating an existing installation, preserve personal home.dat and
nav.dat preferences rather than overwriting them with package defaults.
The add-in uses experimental exception-driven paging; verify in an
emulator before trying physical hardware. The original WinCE MSVC ABI
compatibility profile is NOT included in this package.
"""


def build(source: Path, target: Path) -> None:
    source = source.resolve()
    target = target.resolve()
    if source == target or source in target.parents or target in source.parents:
        raise ValueError("Source and install directory must be separate")
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    loader = source / "NTOS.hh3"
    if not loader.is_file():
        raise FileNotFoundError(loader)
    shutil.copy2(loader, target / loader.name)

    system32 = target / 'Windows' / 'System32'
    system32.mkdir(parents=True)
    pending = list(APPS) + list(EXTRA_MODULES)
    seen: set[str] = set()
    while pending:
        name = pending.pop()
        normalized = name.upper()
        if normalized in seen:
            continue
        original = source / name
        if not original.is_file():
            raise FileNotFoundError(f"Missing required device module: {original}")
        pe = pefile.PE(str(original), fast_load=False)
        if pe.FILE_HEADER.Machine != 0x01A8:
            raise ValueError(f"{name}: not SH4 big-endian PE")
        for imports in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            dep = imports.dll.decode("ascii")
            if "/" in dep or "\\" in dep or dep in (".", ".."):
                raise ValueError(f"Unsafe PE import: {name} -> {dep}")
            candidate = source / dep.upper()
            if not candidate.exists():
                candidate = source / dep
            if not candidate.exists():
                raise FileNotFoundError(f"{name} imports missing {dep}")
            pending.append(candidate.name)
        destination = system32 if normalized.endswith((".DLL", ".CPL")) else target
        shutil.copy2(original, destination / normalized)
        seen.add(normalized)

    windows = source / "Windows" / "demo.cpl"
    if not windows.is_file():
        raise FileNotFoundError(windows)
    (target / "Windows").mkdir(exist_ok=True)
    shutil.copy2(windows, target / "Windows" / "demo.cpl")
    if (system32 / "DEMO.CPL").read_bytes() != windows.read_bytes():
        raise ValueError("DEMO.CPL and Windows/demo.cpl differ")

    apps_dir = source / "Apps"
    if not apps_dir.is_dir():
        raise FileNotFoundError(apps_dir)
    shutil.copytree(apps_dir, target / "Apps")
    if len(list((target / "Apps").glob("*/*.exe"))) != 24:
        raise ValueError("Expected exactly 24 phone applications")

    for name in SIDE_CARS:
        file = source / name
        if file.exists():
            shutil.copy2(file, target / name)

    (target / "INSTALL.txt").write_text(README)
    extras = [x for x in target.iterdir() if x.is_file() and x.suffix in (".elf", ".map")]
    if extras:
        raise AssertionError("Non-installable build files in deployable package")
    assert not (target / "phone-device").exists()
    assert not list(target.glob("*.DLL")), "DLLs must be shared in Windows/System32"
    assert not list(target.glob("*.CPL")), "CPLs must be shared in Windows/System32"
    print(f"Ready to copy: {target} ({len(seen)} PE executables/DLLs, "
          "24 phone apps, Windows/demo.cpl, NTOS.hh3)")
    print("One shared Windows/System32 DLL tree; no test EXEs or ELF files.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--from-dist", type=Path, default=Path("dist"))
    parser.add_argument("--out", type=Path, default=Path("install"))
    args = parser.parse_args()
    build(args.from_dist, args.out)
