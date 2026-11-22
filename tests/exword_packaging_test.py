#!/usr/bin/env python3
"""Verify flat Exword add-in transfer layout and 8.3 import renaming."""
import struct
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1] / "tools"))
from package_exword_flat import shortname,products,package,check

def fake_pe():
    b=bytearray(256);b[0:2]=b"MZ"
    struct.pack_into("<I",b,0x3c,0x80)
    b[0x80:0x86]=b"PE\0\0\xa8\x01"
    return bytes(b)

def main():
    assert shortname("HELLO.EXE")=="HELLO.EXE"
    assert shortname("COREDLL.DLL")=="COREDLL.DLL"
    assert shortname("HELLOCORE.EXE").startswith("HELL")
    assert len(shortname("HELLOCORE.EXE"))==12
    assert shortname("HELLOCORE.EXE")==shortname("hellocore.exe")
    for s in ("abc/def.exe","test:name.dll","invalid.txt","file..dll"):
        try:shortname(s)
        except ValueError:pass
        else:raise AssertionError(s)
    with tempfile.TemporaryDirectory() as temp:
        base=Path(temp);dist=base/"dist";dist.mkdir()
        for name in ("HELLO.EXE","HELLOCORE.EXE","COREDLL.DLL","XAMLUI.DLL",
                     "UIXMOBILE.DLL","CHAIN.DLL","CHAINTEST.EXE","VMTEST.EXE",
                     "OBTEST.EXE","EVTEST.EXE","POOLTEST.EXE","IOTEST.EXE",
                     "PROCTEST.EXE","PROCNEXT.EXE","PROCFINAL.EXE",
                     "MMTEST.EXE","MMROTEST.EXE"):
            (dist/name).write_bytes(fake_pe())
        (dist/"Apps"/"calc").mkdir(parents=True)
        (dist/"Apps"/"calc"/"calc.exe").write_bytes(fake_pe())
        (dist/"Apps"/"edge").mkdir()
        (dist/"Apps"/"edge"/"edge.exe").write_bytes(fake_pe())
        mapping=products(dist)
        assert len(mapping)==19
        for locale in ("ja","cn"):
            folder=base/locale/"NTOSH"
            folder.mkdir(parents=True)
            (folder/"ntos_sh.d01").write_bytes(b"native-D01")
            (folder/"fileinfo.cji").write_bytes(b"")
            (folder/"SYSROOT").mkdir()
            assert package(dist,folder)==19
            check(dist,folder)
            assert not (folder/"SYSROOT").exists()
            assert (folder/shortname("HELLOCORE.EXE")).is_file()
            assert (folder/shortname("UIXMOBILE.DLL")).is_file()
            assert (folder/"COREDLL.DLL").is_file()
            assert not (folder/"Windows").exists()
            assert all(len(p.name)<=12 for p in folder.glob("*.EXE"))
            (dist/"CHAIN.DLL").unlink()
            assert package(dist,folder)==18
            check(dist,folder)
            assert not (folder/"CHAIN.DLL").exists()
            (dist/"CHAIN.DLL").write_bytes(fake_pe())
        (dist/"HELLO.EXE").write_bytes(b"not a PE")
        try:products(dist)
        except ValueError:pass
        else:raise AssertionError("not-PE accepted")
    print("PASS EX-word flat _CONTENT/ with 8.3 EXE/DLL aliases and rebuild cleanup")
if __name__=="__main__":main()
