#!/usr/bin/env python3
"""Host checks for NTOS PE -> EX-word System32/Apps package integration."""
from pathlib import Path
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from package_exword_sysroot import check, package, products, sh4_pe


def fake_pe() -> bytes:
    image = bytearray(256)
    image[0:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, 0x80)
    image[0x80:0x86] = b"PE\0\0\xa8\x01"
    return bytes(image)


def main() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        dist = root / "dist"
        dist.mkdir()
        for name in ("HELLO.EXE", "HELLOCORE.EXE", "COREDLL.DLL",
                     "XAMLUI.DLL", "CHAIN.DLL", "CHAINTEST.EXE"):
            (dist / name).write_bytes(fake_pe())
        apps = dist / "Apps" / "calc"
        apps.mkdir(parents=True)
        (apps / "calc.exe").write_bytes(fake_pe())
        (dist / "calc.uxaml").write_text("native asset")
        win = dist / "Windows"
        win.mkdir()
        (win / "demo.cpl").write_bytes(fake_pe())
        for locale in ("ja", "cn"):
            folder = root / locale / "NTOSH"
            assert package(dist, folder) == 9
            check(dist, folder)
            sysroot = folder / "SYSROOT"
            assert (sysroot / "HELLO.EXE").read_bytes() == fake_pe()
            assert (sysroot / "Apps" / "calc" / "calc.exe").is_file()
            assert (sysroot / "Windows" / "System32" / "XAMLUI.DLL").is_file()
            assert (sysroot / "Windows" / "System32" / "COREDLL.DLL").is_file()
            assert (sysroot / "Windows" / "demo.cpl").is_file()
            assert not (sysroot / "XAMLUI.DLL").exists()
            assert not (sysroot / "DLL").exists()
            (dist / "CHAIN.DLL").unlink()
            assert package(dist, folder) == 8
            assert not (sysroot / "Windows" / "System32" / "CHAIN.DLL").exists()
            check(dist, folder)
            (dist / "CHAIN.DLL").write_bytes(fake_pe())
        (dist / "HELLO.EXE").write_bytes(b"not a PE")
        try:
            products(dist)
        except ValueError as err:
            assert "DOS header" in str(err)
        else:
            raise AssertionError("non-PE .EXE was accepted")
    print("PASS staged EXEs, phone Apps, SH4BE DLL System32, stale cleanup")


if __name__ == "__main__":
    main()
