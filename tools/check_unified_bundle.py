#!/usr/bin/env python3
"""Validate the single native phone/WinCE payload bundle's real invariants.

This does NOT certify Microsoft's ordinal ABI or physical CP400 VBR ownership.
"""
import argparse
import hashlib
from pathlib import Path
import pefile


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def check(root):
    required = ("NTOS.hh3", "COREDLL.DLL", "STARTHOST.EXE",
                "CALC.EXE", "METROWIN.EXE")
    for name in required:
        assert (root / name).is_file(), f"missing {name}"
    device = root / "phone-device"
    assert digest(root / "COREDLL.DLL") == digest(device / "COREDLL.DLL"), (
        "device and WinCE folder use different COREDLLs")
    assert digest(root / "NTOS.hh3") == digest(device / "NTOS.hh3"), (
        "phone and WinCE bundles have mismatched NTOS loader builds")
    apps = sorted((device / "Apps").glob("*/*.exe"))
    assert len(apps) == 24, f"expected 24 phone apps; found {len(apps)}"
    names = ("STARTHOST.EXE", "CALC.EXE", "METROWIN.EXE")
    for name in names:
        pe = pefile.PE(str(root / name), fast_load=False)
        assert pe.FILE_HEADER.Machine == 0x01A8
        for dep in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            depname = dep.dll.decode("ascii").upper()
            assert (root / depname).is_file() or (device / depname).is_file(), (
                name, depname)
    print("PASS one loader, one COREDLL, 24 phone apps, native WinCE-style EXEs")
    print("NOTE Microsoft CE ordinal ABI remains outside this validated bundle")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=Path)
    check(parser.parse_args().bundle)
