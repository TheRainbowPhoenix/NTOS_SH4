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
from generate_apps import generate_headers,generate_assets,icon,APPS

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
MODULES=[
 ('uixrender','UIXRENDER.DLL',{}),
 ('svf','SVF.DLL',{'render_':'UIXRENDER.DLL'}),
 ('catalog','APPCATALOG.DLL',{'svf_':'SVF.DLL','render_':'UIXRENDER.DLL'}),
 ('fingerkb','FINGERKB.DLL',{'render_':'UIXRENDER.DLL','svf_':'SVF.DLL'}),
 ('settingsui','SETTINGSUI.DLL',{'render_':'UIXRENDER.DLL','svf_':'SVF.DLL'}),
 ('shellcontrols','SHELLCONTROLS.DLL',{'render_':'UIXRENDER.DLL'}),
 ('shellchrome','SHELLCHROME.DLL',{'render_':'UIXRENDER.DLL','svf_':'SVF.DLL'}),
 ('uixn','UIXN.DLL',{'render_':'UIXRENDER.DLL','kb_':'FINGERKB.DLL'}),
 ('startmenu','STARTMENU.DLL',{'render_':'UIXRENDER.DLL','catalog_':'APPCATALOG.DLL','settings_':'SETTINGSUI.DLL'}),
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
    ap.add_argument('--msvc',action='store_true',help='use separately generated MSVC PE export adapters')
    ap.add_argument('--pack-only',action='store_true',help='pack ELF files previously built by compile.sh')
    args=ap.parse_args();out=ROOT/args.out;obj=ROOT/args.obj
    out.mkdir(parents=True,exist_ok=True);obj.mkdir(parents=True,exist_ok=True)
    if not args.msvc:generate_headers()
    spec=importlib.util.spec_from_file_location('elf2ntx',ROOT/'tools/elf2ntx.py')
    pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)
    original_import_section=pack.build_import_section
    cc=os.environ.get('PHONE_CC','sh4a_nofpueb-elf-gcc')
    ld=os.environ.get('PHONE_LD','sh4a_nofpueb-elf-g++')
    report=[]
    if not args.pack_only:
        run([cc,'-c',HERE/'tiny_memory.c','-o',obj/'tiny_memory.o','-Os','-ffreestanding','-fno-builtin','-fno-tree-loop-distribute-patterns'])
    for source,module,groups in MODULES:
        dll=module.endswith('.DLL');base=0x10000000 if dll else 0x400000
        o=obj/(source+'.o');elf_path=obj/(source+'.elf')
        if not args.pack_only:
          run([cc,'-c',HERE/(source+'.c'),'-o',o,'-std=c11','-Os','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
             '-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-toplevel-reorder',
             '-fno-unwind-tables','-fno-asynchronous-unwind-tables',
             '-I'+str(HERE),'-I'+str(ROOT/'payload')])
          run([ld,'-nostdlib','-Wl,--emit-relocs','-Wl,-e,_main','-T',
             ROOT/('payload/xamlui.ld' if dll else 'payload/payload.ld'),'-o',elf_path,o,obj/'tiny_memory.o'])
        elf=pack.Elf(elf_path.read_bytes())
        entries=[];implemented=[];stubbed=[]
        if dll:
            manifest=json.loads((HERE/'abi'/(module.lower()+'.json')).read_text())
            # Strip exactly the SH compiler's one leading underscore.
            defined={s.name[1:] if s.name.startswith('_') else s.name:s.value
                     for s in elf.symbols if s.shndx!=pack.SHN_UNDEF and s.shndx<pack.SHN_LORESERVE}
            stub=defined['msvc_unsupported' if args.msvc else 'phone_unsupported']-base
            for e in manifest['exports']:
                name=e['name'];address=defined.get(('msvc_export_'+name) if args.msvc and name else name)
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
    records=generate_assets(device)
    for app in records:
        slug=app['slug'];id_=app['id'];elf_path=obj/('app-'+slug+'.elf');o=obj/('app-'+slug+'.o')
        if not args.pack_only:
            run([cc,'-c',HERE/'stubapp.c','-o',o,'-std=c11','-Os','-ffreestanding','-fno-builtin','-fno-toplevel-reorder','-fno-stack-protector','-DAPP_ID='+str(id_),'-I'+str(HERE),'-I'+str(ROOT/'payload')])
            run([ld,'-nostdlib','-Wl,--emit-relocs','-Wl,-e,_main','-T',ROOT/'payload/payload.ld','-o',elf_path,o,obj/'tiny_memory.o'])
        elf=pack.Elf(elf_path.read_bytes());groups={'mobile_':'UIXMOBILE.DLL','menu_':'STARTMENU.DLL'}
        binary,_=pack.emit_ntx(elf,0x400000,import_module='COREDLL.DLL',extra_imports=list(groups.items()))
        if slug=='calc':binary=(out/'CALC.EXE').read_bytes()
        (device/'Apps'/slug/(slug+'.exe')).write_bytes(add_icon_resource(binary,icon(slug)))
    if (out/'NTOS.hh3').exists():shutil.copyfile(out/'NTOS.hh3',device/'NTOS.hh3')
    shutil.copyfile(HERE/'README.md',device/'README.md')
    print('Generated',len(records),'real SH4 app EXEs with embedded SVF and binary sidecars')
    print('Phone modules:',sum(r['bytes'] for r in report),'bytes; COREDLL supplied by make ntx/phone')

def add_icon_resource(binary,svf):
    """Append a real RT_RCDATA/1/1033 resource without changing code RVAs."""
    b=bytearray(binary);pe=struct.unpack_from('<I',b,60)[0];coff=pe+4;opt=coff+20
    count=struct.unpack_from('<H',b,coff+2)[0];assert count<8
    table=opt+struct.unpack_from('<H',b,coff+16)[0]
    old_headers=struct.unpack_from('<I',b,opt+60)[0]
    new_headers=(table+40*(count+1)+511)&~511;shift=max(0,new_headers-old_headers)
    if shift:
        b[old_headers:old_headers]=bytes(shift)
        for i in range(count):
            p=table+40*i+20;raw=struct.unpack_from('<I',b,p)[0]
            if raw:struct.pack_into('<I',b,p,raw+shift)
    struct.pack_into('<I',b,opt+60,max(old_headers,new_headers))
    va=(struct.unpack_from('<I',b,opt+56)[0]+511)&~511
    resource=bytearray(88)
    for at in [0,24,48]:struct.pack_into('<H',resource,at+14,1)
    struct.pack_into('<II',resource,16,10,0x80000018)
    struct.pack_into('<II',resource,40,1,0x80000030)
    struct.pack_into('<II',resource,64,1033,72)
    struct.pack_into('<IIII',resource,72,va+88,len(svf),0,0);resource.extend(svf)
    raw=(len(b)+511)&~511;b.extend(bytes(raw-len(b)));size=(len(resource)+511)&~511
    b.extend(resource);b.extend(bytes(size-len(resource)))
    struct.pack_into('<8sIIIIIIHHI',b,table+40*count,b'.rsrc\0\0\0',len(resource),va,size,raw,0,0,0,0,0x40000040)
    struct.pack_into('<H',b,coff+2,count+1);struct.pack_into('<I',b,opt+56,va+size)
    struct.pack_into('<II',b,opt+96+16,va,len(resource))
    return bytes(b)

if __name__=='__main__':main()
