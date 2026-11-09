#!/usr/bin/env python3
"""Build the small UIX profile using the repository's SH4BE ELF/PE writer.

Preserves reference named AND ordinal-only exports, including non-1 bases.
No modification to the shipping packer's behavior for other payloads.
"""
import argparse
import importlib.util
import json
import os
import shutil
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
MODULES=[
 ('uixrender','UIXRENDER.DLL',{}),
 ('shellcontrols','SHELLCONTROLS.DLL',{'render_':'UIXRENDER.DLL'}),
 ('shellchrome','SHELLCHROME.DLL',{'render_':'UIXRENDER.DLL'}),
 ('uixn','UIXN.DLL',{'render_':'UIXRENDER.DLL'}),
 ('startmenu','STARTMENU.DLL',{'render_':'UIXRENDER.DLL','controls_':'SHELLCONTROLS.DLL'}),
 ('uixmobile','UIXMOBILE.DLL',{'engine_':'UIXN.DLL','menu_':'STARTMENU.DLL','chrome_':'SHELLCHROME.DLL'}),
 ('starthost','STARTHOST.EXE',{'mobile_':'UIXMOBILE.DLL','menu_':'STARTMENU.DLL'}),
]

def run(args):
    print(' '.join(str(a) for a in args),flush=True)
    subprocess.run([str(a) for a in args],check=True,cwd=ROOT)

def export_section(entries,module,rva):
    """entries = (ordinal, optional name, function RVA), PE little endian."""
    base=min(e[0] for e in entries)
    count=max(e[0] for e in entries)-base+1
    names=sorted((n,o) for o,n,_ in entries if n is not None)
    blob=bytearray(40)
    module_offset=len(blob);blob.extend(module.encode('ascii')+b'\0')
    while len(blob)%4:blob.append(0)
    eat=len(blob);blob.extend(bytes(4*count))
    for ordinal,_,address in entries:struct.pack_into('<I',blob,eat+4*(ordinal-base),address)
    npt=len(blob);blob.extend(bytes(4*len(names)))
    ords=len(blob);blob.extend(bytes(2*len(names)))
    for i,(name,ordinal) in enumerate(names):
        struct.pack_into('<I',blob,npt+4*i,rva+len(blob))
        struct.pack_into('<H',blob,ords+2*i,ordinal-base)
        blob.extend(name.encode('ascii')+b'\0')
    struct.pack_into('<IIHHIIIIIII',blob,0,0,0,0,0,rva+module_offset,
                     base,count,len(names),rva+eat,rva+npt,rva+ords)
    return bytes(blob)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',default='dist');ap.add_argument('--obj',default='obj/phone')
    ap.add_argument('--pack-only',action='store_true',help='pack ELF files previously built by compile.sh')
    args=ap.parse_args();out=ROOT/args.out;obj=ROOT/args.obj
    out.mkdir(parents=True,exist_ok=True);obj.mkdir(parents=True,exist_ok=True)
    spec=importlib.util.spec_from_file_location('elf2ntx',ROOT/'tools/elf2ntx.py')
    pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)
    original_import_section=pack.build_import_section
    cc=os.environ.get('PHONE_CC','sh4a_nofpueb-elf-gcc')
    ld=os.environ.get('PHONE_LD','sh4a_nofpueb-elf-g++')
    report=[]
    for source,module,groups in MODULES:
        dll=module.endswith('.DLL');base=0x10000000 if dll else 0x400000
        o=obj/(source+'.o');elf_path=obj/(source+'.elf')
        if not args.pack_only:
          run([cc,'-c',HERE/(source+'.c'),'-o',o,'-std=c11','-Os','-Wall','-Wextra','-Werror',
             '-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-toplevel-reorder',
             '-fno-unwind-tables','-fno-asynchronous-unwind-tables',
             '-I'+str(HERE),'-I'+str(ROOT/'payload')])
          run([ld,'-nostdlib','-Wl,--emit-relocs','-Wl,-e,_main','-T',
             ROOT/('payload/xamlui.ld' if dll else 'payload/payload.ld'),'-o',elf_path,o])
        elf=pack.Elf(elf_path.read_bytes())
        entries=[];implemented=[];stubbed=[]
        if dll:
            manifest=json.loads((HERE/'abi'/(module.lower()+'.json')).read_text())
            # Strip exactly the SH compiler's one leading underscore.
            defined={s.name[1:] if s.name.startswith('_') else s.name:s.value
                     for s in elf.symbols if s.shndx!=pack.SHN_UNDEF and s.shndx<pack.SHN_LORESERVE}
            stub=defined['phone_unsupported']-base
            for e in manifest['exports']:
                name=e['name'];address=defined.get(name)
                entries.append((e['ordinal'],name,address-base if address is not None else stub))
                (implemented if address is not None else stubbed).append(name or '#'+str(e['ordinal']))
            pack.build_export_section=lambda unused,n,r,entries=entries:export_section(entries,n,r)
        def imports(import_groups,rva):
            # Prefixes distinguish same-named exports in different modules;
            # only the PE names lose that private C namespace.
            clean=[]
            for target,slots in import_groups:
                prefix=next((p for p,m in groups.items() if m==target),'')
                clean.append((target,[(slot,name[len(prefix):] if prefix else name) for slot,name in slots]))
            return original_import_section(clean,rva)
        pack.build_import_section=imports
        has_imports=bool(pack.collect_import_slots(elf))
        binary,sites=pack.emit_ntx(elf,base,exports=[('surface',0)] if dll else None,
            dll_name=module,import_module='COREDLL.DLL' if has_imports else None,
            image_is_dll=dll,extra_imports=list(groups.items()))
        (out/module).write_bytes(binary)
        report.append({'module':module,'bytes':len(binary),'base':hex(base),'relocations':len(sites),
                       'implemented':implemented,'stubbed':stubbed})
        print(f'{module}: {len(binary)} bytes, {len(sites)} relocations, {len(implemented)} implemented, {len(stubbed)} stubs')
    (out/'phone-build.json').write_text(json.dumps(report,indent=2)+'\n')
    device=out/'phone-device';device.mkdir(exist_ok=True)
    for name in [r['module'] for r in report]+['COREDLL.DLL']:
        if (out/name).exists():shutil.copyfile(out/name,device/name)
    print('Phone modules:',sum(r['bytes'] for r in report),'bytes; COREDLL supplied by make ntx/phone')

if __name__=='__main__':main()
