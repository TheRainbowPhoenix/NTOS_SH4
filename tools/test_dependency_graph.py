#!/usr/bin/env python3
"""Independent PE import graph test over actual shipping DLLs.

The device loader discovers DLL names recursively from PE import
descriptors, deduplicates case-insensitively, and performs import snaps
only once every module has been mapped. Compare its documented behavior
against real PE import data and cyclic/transitive synthetic graphs.
"""
from collections import deque
from verify_pe_linkage import PE

def discover(root, lookup, limit=10):
    queue=deque([root.upper()])
    loaded=[]
    seen=set()
    while queue:
        name=queue.popleft()
        if name in seen:
            continue
        if len(loaded)>=limit:
            raise ValueError("module table full")
        if name not in lookup:
            raise FileNotFoundError(name)
        seen.add(name)
        loaded.append(name)
        for dep in lookup[name]:
            dep=dep.upper()
            if dep not in seen and dep not in queue:
                queue.append(dep)
    return loaded

def main():
    images={
        "COREDLL.DLL":PE("dist/COREDLL.DLL"),
        "XAMLUI.DLL":PE("dist/XAMLUI.DLL"),
        "CHAIN.DLL":PE("dist/CHAIN.DLL"),
        "METROWIN.EXE":PE("dist/METROWIN.EXE"),
        "CHAINTEST.EXE":PE("dist/CHAINTEST.EXE"),
        "STRESSEXEC.EXE":PE("dist/STRESSEXEC.EXE"),
        **{f"ST{i:02}.DLL":PE(f"dist/ST{i:02}.DLL") for i in range(1,13)},
    }
    imports={name:list(img.imports()) for name,img in images.items()}
    assert imports["CHAINTEST.EXE"]==["CHAIN.DLL"],imports
    assert imports["CHAIN.DLL"]==["COREDLL.DLL"],imports
    chain=discover("chaintest.exe",imports)
    assert chain==["CHAINTEST.EXE","CHAIN.DLL","COREDLL.DLL"],chain
    metro=discover("metrowin.exe",imports)
    assert metro==["METROWIN.EXE","COREDLL.DLL","XAMLUI.DLL"],metro

    stress=discover("STRESSEXEC.EXE",imports,limit=16)
    assert stress[0]=="STRESSEXEC.EXE"
    assert set(stress)==({"STRESSEXEC.EXE","COREDLL.DLL"} |
                        {f"ST{i:02}.DLL" for i in range(1,13)}),stress
    assert set(imports["ST01.DLL"])=={
        "ST02.DLL","ST03.DLL","ST04.DLL","ST05.DLL"}
    print("PASS 12 stress DLLs plus COREDLL, four-way import fanout")
    synthetic={"A.EXE":["B.DLL","C.DLL"],
               "B.DLL":["C.DLL"],"C.DLL":["B.DLL"]}
    assert discover("A.EXE",synthetic)==["A.EXE","B.DLL","C.DLL"]
    try:
        discover("A.EXE",{"A.EXE":["MISSING.DLL"]})
        raise AssertionError("missing module incorrectly allowed")
    except FileNotFoundError:
        pass
    try:
        discover("A.EXE",synthetic,limit=2)
        raise AssertionError("module-table overflow incorrectly allowed")
    except ValueError:
        pass
    print("PASS recursive PE dependency discovery with real EXE->DLL->DLL chain")
    print("PASS shared deps/cycles, missing modules and registry limit")

if __name__=="__main__":
    main()
