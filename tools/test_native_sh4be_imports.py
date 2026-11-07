#!/usr/bin/env python3
"""Verify native GNU ld SH4BE DLL exports and chained PE imports.

Not an ELF-to-PE converter: only inspects PE binaries built by GNU ld.
Uses the same independent binary PE reader as the production linker test.
"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_pe_linkage import PE


def main():
    root=Path("/tmp/ntos-native")
    core=PE(root / "nativebase.dll")
    layer=PE(root / "nativelayer.dll")
    app=PE(root / "nativeclient.exe")
    assert core.dll and layer.dll and not app.dll
    assert [core.base,layer.base,app.base]==[0x800000,0xc00000,0x400000]
    assert all(p.machine == 0x01a7 for p in (core,layer,app))
    ce=[name for name in core.exports() if name.endswith("core_add")]
    le=[name for name in layer.exports() if name.endswith("layer_sum")]
    assert ce,(list(core.exports())[:15],"missing base export")
    assert le,(list(layer.exports())[:15],"missing layer export")
    edge1=layer.imports()
    edge2=app.imports()
    assert len(edge1) == 1,(edge1,"DLL-to-DLL import descriptor missing")
    assert len(edge2) == 1,(edge2,"EXE-to-DLL import descriptor missing")
    assert "NATIVEBASE.DLL" in edge1,edge1
    assert "NATIVELAYER.DLL" in edge2,edge2
    for consumer,providers in ((layer,{"NATIVEBASE.DLL":core}),
                               (app,{"NATIVELAYER.DLL":layer})):
        for dll,functions in consumer.imports().items():
            exported=providers[dll].exports()
            for name,iat in functions:
                assert name in exported,(consumer.path,dll,name)
                target=providers[dll].base+exported[name]
                assert consumer.memory[iat:iat+4]==b"\0"*4
                assert target>=providers[dll].base+0x1000
                print(f"PASS {consumer.path.name} imports {dll}!{name} "
                      f"-> {target:#010x}, BE IAT slot RVA {iat:#x}")
    print("PASS native GNU ld EXE -> DLL -> DLL named import chain")


if __name__=="__main__":
    main()
