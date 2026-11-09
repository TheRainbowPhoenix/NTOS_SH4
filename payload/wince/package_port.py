#!/usr/bin/env python3
"""Package local experimental outputs and reference pseudocode; never publish."""
import json
from pathlib import Path
import zipfile
import pefile
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
OUT=ROOT/'dist/ce-demo'

def main():
    for module in ('COREDLL.DLL','COMMCTRL.DLL'):
        reference=json.loads((HERE/'abi'/(module.lower()+'.json')).read_text())
        pe=pefile.PE(str(OUT/module))
        assert pe.FILE_HEADER.Machine==0x1a8
        exports={e.ordinal:e.name.decode() if e.name else None for e in pe.DIRECTORY_ENTRY_EXPORT.symbols}
        for e in reference['exports']:
            assert e['ordinal'] in exports and exports[e['ordinal']]==e['name'],(module,e)
    control=json.loads((OUT/'control-runtime.json').read_text())
    assert control['status']=='window-and-message-loop' and not control['unsupported']
    assert all(v==0 for v in json.loads((OUT/'detach-runtime.json').read_text()).values())
    assert not json.loads((OUT/'port-status.json').read_text())['control_untyped_imports']
    decomp=ROOT/'obj/wince/commctrl-decomp'
    assert (decomp/'complete.txt').read_text().strip()=='1414/1414'
    assert len(list(decomp.glob('*.c')))==1414
    source=ROOT/'dist/wince-commctrl-source.zip'
    with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED) as z:
        for f in HERE.rglob('*'):
            if f.is_file() and '__pycache__' not in f.parts:
                z.write(f,str(f.relative_to(ROOT)))
        for name in ['src/main.cpp','src/console.hpp','tests/console_host_test.cpp','tools/clemu_wince_smoke.py']:
            z.write(ROOT/name,name)
        for f in decomp.iterdir():
            if f.is_file():z.write(f,'reference/commctrl-decomp/'+f.name)
        z.writestr('reference/README.txt','Full Ghidra reference pseudocode from the user-provided local COMMCTRL. These files are not build inputs and are not a complete compilable C port. See payload/wince/README.md.\n')
    binary=ROOT/'dist/wince-msvc-compat.zip'
    with zipfile.ZipFile(binary,'w',zipfile.ZIP_DEFLATED) as z:
        names=['NTOS.hh3','COREDLLABI.EXE','shell.exe','control.exe','toolhelp.dll','COREDLL.DLL','COMMCTRL.DLL',
               'runtime-results.json','shell-runtime-low.json','control-runtime.json',
               'detach-runtime.json','port-status.json','shell-output.txt']
        for name in names:z.write(OUT/name,name)
        z.write(HERE/'README.md','README.md')
        for case in ['control-check','abi-final','shell-40']:
            folder=ROOT/('obj/wince/full-smoke-'+case)
            for f in folder.glob('*.png'):z.write(f,'verification/'+case+'/'+f.name)
            if (folder/'result.json').exists():z.write(folder/'result.json','verification/'+case+'/result.json')
        z.write(ROOT/'tools/clemu_wince_smoke.py','verification/clemu_wince_smoke.py')
        z.write(Path(r'C:/Users/Phoebe/Documents/git/clemu-310c/ntos_launch.py'),'launcher/ntos_launch.py')
        for name in ('lint-final.log','control-final.log','shell-final.log'):
            z.write(ROOT/'obj/wince'/name,'verification/'+name)
    for f in (source,binary):print(f.name,f.stat().st_size,'bytes')
    print('PASS: exact reference ordinals/names preserved; original control reaches its UI loop; DLL registries clear')
if __name__=='__main__':main()

