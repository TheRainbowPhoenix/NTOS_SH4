#!/usr/bin/env python3
"""Ensure one shared loader/COREDLL and one flat root for native phone apps."""
import argparse
from pathlib import Path

import pefile


def check(root):
    required = (
        "NTOS.hh3", "COREDLL.DLL", "STARTHOST.EXE", "CALC.EXE",
        "METROWIN.EXE", "CONTROL.EXE", "DEMO.CPL",
    )
    for name in required:
        assert (root / name).is_file(), f"missing {name}"
    assert (root / "Windows/demo.cpl").is_file(), "Windows/demo.cpl not installed"
    assert (root / "DEMO.CPL").read_bytes() == (root / "Windows/demo.cpl").read_bytes(), (
        "the installed demo CPL differs from the linked module")
    assert not (root / "phone-device").exists(), "obsolete phone-device tree"
    apps = sorted((root / "Apps").glob("*/*.exe"))
    assert len(apps) == 24, f"expected 24 phone apps; found {len(apps)}"
    for name in ("STARTHOST.EXE", "CALC.EXE", "METROWIN.EXE", "CONTROL.EXE"):
        pe = pefile.PE(str(root / name))
        assert pe.FILE_HEADER.Machine == 0x01A8
        for dep in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            dll = dep.dll.decode("ascii").upper()
            assert (root / dll).is_file(), (name, "missing dependency", dll)
            actual = pefile.PE(str(root / dll))
            names = {e.name for e in actual.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
            for symbol in dep.imports:
                if symbol.name:
                    assert symbol.name in names, (name, dll, symbol.name)
    print("PASS flat distribution: one COREDLL, CONTROL.EXE, DEMO.CPL and 24 phone apps")
    print("Original Microsoft ordinal imports need separate runtime validation")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=Path)
    check(parser.parse_args().bundle)
