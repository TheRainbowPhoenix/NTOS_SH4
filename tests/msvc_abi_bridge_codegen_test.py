#!/usr/bin/env python3
"""Build-time checks for *measured* five/eight-word Microsoft SH4 ABI stubs.

These are structural assertions, not an emulation of SH4 hardware.
Runtime execution of the linked EXE/DLL pairs is required in CP400 emulator.
"""
import importlib.util
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
GEN = ROOT / "tools" / "gen_sh4_msvc_thunks.py"


def fail(message):
    raise AssertionError(message)


def check_generator():
    spec = importlib.util.spec_from_file_location("ntos_msvc_thunks", GEN)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    exp = mod.generate("export")
    imp = mod.generate("import")
    assert len(mod.SPECS) == 2
    assert {n for _, n, _ in mod.SPECS} == {5, 8}
    for name, n, native in mod.SPECS:
        assert ("_" + name + ":") in exp
        assert ("_ntos_msvc_import" + str(n) + ":") in imp
        assert ("_" + native) in exp
        assert ("___imp_" + name) in imp
        # Per-wrapper emitted stack word transfers, not an ABI cast.
        e = exp.split("_" + name + ":")[1].split(".Lnative_" + str(n))[0]
        i = imp.split("_ntos_msvc_import" + str(n) + ":")[1].split(".Liat_" + str(n))[0]
        assert e.count("    mov.l @r1+,r0") == n - 4
        assert i.count("    mov.l @r1+,r0") == n - 4
        assert "    add #36,r1" in e  # S+16, after 20-byte local frame
        assert "    add #40,r1" in i  # GCC S+0, after 40-byte local frame
        for k in range(4):
            assert ("    mov.l r%d,@(%d,r1)" % (k+4,4*k)) in e if k else "    mov.l r4,@r1" in e
        assert "    add #-32,r15" in i  # 16 home + 16 overflow
        assert "    stc.l gbr,@-r15" in i
    assert "fr4" not in exp.lower()
    assert "fr4" not in imp.lower()


def check_pe():
    import pefile
    for path, dllname in [
        ("dist_msvc_global/ABIGLOBAL.DLL", "ABIGLOBAL.DLL"),
        ("dist_msvc_thunks/ABITHUNKS.DLL", "ABITHUNKS.DLL"),
    ]:
        pe = pefile.PE(str(ROOT / path))
        assert pe.FILE_HEADER.Machine == 0x01A8  # NTOS BE private legacy tag
        assert pe.OPTIONAL_HEADER.Subsystem == 9
        exports = {s.name.decode("ascii") for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name}
        assert {"abi_msvc5", "abi_msvc8"} <= exports, (path, exports)
    for path, dllname in [
        ("dist_msvc_global/ABIGLOBAL.EXE", "ABIGLOBAL.DLL"),
        ("dist_msvc_thunks/ABITHUNKS.EXE", "ABITHUNKS.DLL"),
    ]:
        pe = pefile.PE(str(ROOT / path))
        assert pe.FILE_HEADER.Machine == 0x01A8
        dependencies = {d.dll.decode("ascii").upper():d for d in pe.DIRECTORY_ENTRY_IMPORT}
        assert dllname in dependencies, (path, dependencies)
        names = {x.name.decode("ascii") for x in dependencies[dllname].imports if x.name}
        assert {"abi_msvc5","abi_msvc8"} <= names, (path,names)


if __name__ == "__main__":
    check_generator()
    if "--pe" in sys.argv:
        check_pe()
    print("PASS MSVC SH4 word-thunk generation and PE import/export shapes (runtime not tested)")
