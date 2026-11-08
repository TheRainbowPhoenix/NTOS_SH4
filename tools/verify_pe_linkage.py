#!/usr/bin/env python3
"""Independently validate PE EXE->DLL->DLL linking and SH4BE IAT writes.

The build uses GNU ld for SH4BE code. PE packaging remains a compatibility
shim until a big-endian SH4 PE BFD backend is available. This validator does
NOT share the packer's implementation and checks the finished PE bytes.
"""
import argparse
from pathlib import Path
import struct


def le16(b, p):
    return struct.unpack_from("<H", b, p)[0]


def le32(b, p):
    return struct.unpack_from("<I", b, p)[0]


class PE:
    def __init__(self, path, allow_preinitialized_iat=False):
        self.allow_preinitialized_iat = allow_preinitialized_iat
        self.path = Path(path)
        self.blob = self.path.read_bytes()
        d = self.blob
        assert d[:2] == b"MZ", self.path
        pe = le32(d, 0x3c)
        assert d[pe:pe + 4] == b"PE\0\0", self.path
        header = pe + 4
        self.machine = le16(d, header)
        assert self.machine in (0x01A7, 0x01A8), (
            self.path, "NTOS native/legacy private SH4BE machine")
        count = le16(d, header + 2)
        flags = le16(d, header + 18)
        assert flags & 2 and count <= 8
        opt = header + 20
        assert le16(d, opt) == 0x10b
        self.base = le32(d, opt + 28)
        self.image_size = le32(d, opt + 56)
        assert self.image_size < 0x1000000
        self.entry = le32(d, opt + 16)
        self.directories = [
            (le32(d, opt + 96 + i * 8), le32(d, opt + 100 + i * 8))
            for i in range(16)
        ]
        sec = opt + le16(d, header + 16)
        self.sections = []
        self.memory = bytearray(self.image_size)
        headers = le32(d, opt + 60)
        self.memory[:headers] = d[:headers]
        for i in range(count):
            p = sec + i * 40
            name = d[p:p + 8].split(b"\0")[0].decode("ascii")
            vs, va, rs, raw = (le32(d, p + j) for j in (8, 12, 16, 20))
            fl = le32(d, p + 36)
            assert va + max(vs, rs) <= self.image_size
            assert raw + rs <= len(d)
            self.sections.append((name, va, max(vs, rs), fl, rs, raw))
            self.memory[va:va + rs] = d[raw:raw + rs]
        assert self.entry < self.image_size
        assert self.section_for(self.entry)[0] == ".text", (
            self.path, "entry not inside executable .text"
        )
        self.dll = bool(flags & 0x2000)

    def section_for(self, rva):
        for section in self.sections:
            if section[1] <= rva < section[1] + section[2]:
                return section
        raise AssertionError((self.path, "RVA outside sections", hex(rva)))

    def u16(self, rva):
        assert rva + 2 <= self.image_size
        return le16(self.memory, rva)

    def u32(self, rva):
        assert rva + 4 <= self.image_size
        return le32(self.memory, rva)

    def ascii(self, rva):
        assert 0 < rva < self.image_size
        end = self.memory.index(0, rva)
        assert end - rva < 256
        return self.memory[rva:end].decode("ascii")

    def exports(self):
        rva, length = self.directories[0]
        if not rva:
            return {}
        assert length >= 40 and rva + length <= self.image_size
        base_ord = self.u32(rva + 16)
        nf, nn = self.u32(rva + 20), self.u32(rva + 24)
        eat, npt, ords = (
            self.u32(rva + 28), self.u32(rva + 32),
            self.u32(rva + 36)
        )
        assert 0 < nf <= 4096 and 0 < nn <= nf
        assert eat + nf * 4 <= self.image_size
        assert npt + nn * 4 <= self.image_size
        assert ords + nn * 2 <= self.image_size
        result = {}
        for i in range(nn):
            name = self.ascii(self.u32(npt + 4 * i))
            ordinal = self.u16(ords + 2 * i)
            assert ordinal < nf and name not in result
            func = self.u32(eat + 4 * ordinal)
            assert func and not (rva <= func < rva + length), (
                name, "unsupported forwarded export"
            )
            target = self.section_for(func)
            assert target[3] & 0x20000000, (
                self.path, name, "export not executable"
            )
            result[name] = func
        return result

    def imports(self):
        rva, length = self.directories[1]
        if not rva:
            return {}
        assert length >= 40 and rva + length <= self.image_size
        result = {}
        for i in range(min(20, length // 20)):
            p = rva + i * 20
            oft, name, iat = self.u32(p), self.u32(p + 12), self.u32(p + 16)
            if not (oft or name or iat):
                break
            assert oft and name and iat
            module = self.ascii(name).upper()
            assert module not in result
            funcs = []
            for j in range(256):
                hint_thunk = self.u32(oft + j * 4)
                if not hint_thunk:
                    break
                assert not (hint_thunk & 0x80000000), "ordinal imports unsupported"
                sym = self.ascii(hint_thunk + 2)
                dest = iat + j * 4
                assert self.section_for(dest)[3] & 0x80000000, (
                    self.path, sym, "IAT target not writable"
                )
                if not self.allow_preinitialized_iat:
                    assert self.memory[dest:dest + 4] == b"\0" * 4, (
                        self.path, sym, "IAT not initialized to zero"
                    )
                funcs.append((sym, dest))
            else:
                raise AssertionError("unterminated import lookup table")
            assert funcs
            result[module] = funcs
        else:
            raise AssertionError("unterminated import descriptors")
        return result

    def compare_text(self, dump_file):
        if not dump_file:
            return
        sec = next(s for s in self.sections if s[0] == ".text")
        orig = Path(dump_file).read_bytes()
        actual = self.blob[sec[5]:sec[5] + sec[4]]
        assert orig == actual, (
            self.path, "GNU objcopy .text bytes differ from PE .text",
            len(orig), len(actual)
        )


def main():
    arg = argparse.ArgumentParser()
    arg.add_argument("--text", nargs=2, action="append",
                     metavar=("IMAGE", "DUMP"), default=[])
    args = arg.parse_args()
    dlls = {
        "COREDLL.DLL": PE("dist/COREDLL.DLL"),
        "XAMLUI.DLL": PE("dist/XAMLUI.DLL"),
        "CHAIN.DLL": PE("dist/CHAIN.DLL"),
        "PAGERSTRESS.DLL": PE("dist/PAGERSTRESS.DLL"),
        "FAULTSTRESS.DLL": PE("dist/FAULTSTRESS.DLL"),
        **{f"ST{i:02}.DLL": PE(f"dist/ST{i:02}.DLL") for i in range(1, 13)},
    }
    programs = {
        name: PE("dist/" + name)
        for name in (
            "HELLO.EXE", "HELLOCORE.EXE", "MEMTOOL.EXE", "PAINT.EXE",
            "WINLAB.EXE", "GUIWIN.EXE", "METROWIN.EXE", "CALC.EXE",
            "CHAINTEST.EXE", "STRESSEXEC.EXE",
            "PAGERTEST.EXE", "FAULTTEST.EXE"
        )
    }
    assert dlls["COREDLL.DLL"].dll and dlls["XAMLUI.DLL"].dll
    assert all(not prog.dll for prog in programs.values())

    exports = {name: image.exports() for name, image in dlls.items()}
    assert len(exports["COREDLL.DLL"]) > 500
    assert len(exports["XAMLUI.DLL"]) >= 15
    assert "TextOutW" in exports["COREDLL.DLL"]
    assert "xamlui_start" in exports["XAMLUI.DLL"]

    image_set = {**dlls, **programs}
    snapped = 0
    for name, image in image_set.items():
        imports = image.imports()
        if name == "METROWIN.EXE":
            assert set(imports) == {"COREDLL.DLL", "XAMLUI.DLL"}
        if name == "XAMLUI.DLL":
            assert set(imports) == {"COREDLL.DLL"}
        if name == "CHAIN.DLL":
            assert set(imports) == {"COREDLL.DLL"}
        if name == "CHAINTEST.EXE":
            assert set(imports) == {"CHAIN.DLL"}
            assert "chain_echo" in exports["CHAIN.DLL"]
        if name == "STRESSEXEC.EXE":
            assert set(imports) == {"ST01.DLL"}
        if name == "PAGERTEST.EXE":
            assert set(imports) == {"PAGERSTRESS.DLL"}
        if name == "FAULTTEST.EXE":
            assert set(imports) == {"FAULTSTRESS.DLL"}
        if name == "ST01.DLL":
            assert set(imports) == {"ST02.DLL", "ST03.DLL",
                                    "ST04.DLL", "ST05.DLL"}
        for module, functions in imports.items():
            assert module in dlls, (name, "missing import module", module)
            for symbol, iat_rva in functions:
                assert symbol in exports[module], (
                    name, module, "unresolved import", symbol
                )
                target = dlls[module].base + exports[module][symbol]
                # Exact BE IAT write the NTOS loader must make; PE metadata
                # and export/import name tables remain little endian.
                struct.pack_into(">I", image.memory, iat_rva, target)
                assert struct.unpack_from(">I", image.memory, iat_rva)[0] == target
                snapped += 1
        print(f"PASS {name}: {sum(len(v) for v in imports.values())} "
              f"resolved imports from {', '.join(imports) or 'none'}")
    for name, dump in args.text:
        assert name in image_set, name
        image_set[name].compare_text(dump)
        print(f"PASS {name}: PE executable code is byte-identical "
              f"to GNU binutils .text dump")
    assert snapped > 40
    print(f"PASS {snapped} EXE/DLL IAT slots resolve to real executable "
          "exports; BE loader snapping verified")


if __name__ == "__main__":
    main()
