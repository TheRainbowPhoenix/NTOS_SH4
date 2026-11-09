#!/usr/bin/env python3
"""Build an isolated two-sided scalar MSVC PE-boundary phone experiment.
Private UI tables, NTOS gates and window callbacks remain GCC-native by design.
"""
import importlib.util,json,re,subprocess,sys,shlex,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OBJ=ROOT/'obj/phone-msvc';OUT=ROOT/'dist/phone-msvc';OBJ.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(HERE));import build_phone as phone
spec=importlib.util.spec_from_file_location('pack',ROOT/'tools/elf2ntx.py');pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)
def run_script(lines):
    path=OBJ/'build.sh';path.write_bytes(('set -eu\nexport PATH=/toolchain/bin:$PATH\n'+'\n'.join(lines)+'\n').encode())
    subprocess.run(['docker','run','--rm','-v',str(ROOT)+':/work','-w','/work','ghcr.io/classpaddev/hollyhock-3:v2.2.1','sh','obj/phone-msvc/build.sh'],check=True)
def relative(path):return path.relative_to(ROOT).as_posix()
def arity(params):
    if params.strip() in ('','void'):return 0
    if '...' in params or re.search(r'\b(float|double|long long|uint64_t|int64_t)\b',params):raise ValueError('non-word/variadic signature: '+params)
    depth=0;n=1
    for ch in params:
        if ch=='(':depth+=1
        elif ch==')':depth-=1
        elif ch==',' and not depth:n+=1
    if n>16:raise ValueError('more than sixteen words')
    return n
HEADER=r'''#include "ntimport.h"
#undef NT_IMPORT
#define NT_IMPORT(ret,name,params) \
 typedef ret (*__ntx_fn_t_##name) params; \
 extern const void *volatile __imp_##name; \
 extern ret __nt_msvc_import_##name params; \
 static __inline __ntx_fn_t_##name __ntx_ref_##name(void) {return &__nt_msvc_import_##name;}
'''
(OBJ/'imports.h').write_text(HEADER)
units=[(s,HERE/(s+'.c'),None) for s,_,_ in phone.MODULES]+[('core',ROOT/'payload/coredll.c',None),('calc',ROOT/'payload/calc.c',None)]
units += [('app-'+a['slug'],HERE/'stubapp.c',a['id']) for a in phone.generate_assets(OUT)]
flags='-std=c11 -Os -Wall -Wextra -Werror -Wno-misleading-indentation -Wno-unused-function -ffreestanding -fno-builtin -fno-stack-protector -fno-toplevel-reorder -fno-unwind-tables -fno-asynchronous-unwind-tables -Ipayload/phone -Ipayload -include obj/phone-msvc/imports.h'
lines=['sh4a_nofpueb-elf-gcc -c payload/phone/tiny_memory.c -o obj/phone-msvc/tiny_memory.o -Os -ffreestanding -fno-builtin']
for name,source,id_ in units:
    extra='' if id_ is None else ' -DAPP_ID='+str(id_)
    lines.append(f'sh4a_nofpueb-elf-gcc {flags}{extra} -aux-info obj/phone-msvc/{name}.aux -c {relative(source)} -o obj/phone-msvc/{name}.o')
run_script(lines)
# Both directions preserve GBR/PR/r8. Counts are read from compiler prototypes,
# not guessed from names or copied past the declared argument area.
DISPATCH='''.text
.align 2
_phone_export:
mov.l r8,@-r15
stc.l gbr,@-r15
sts.l pr,@-r15
add #-64,r15
mov r0,r8
mov r1,r2
add #-4,r2
cmp/pl r2
bf .Lecall
mov r15,r3
add #92,r3
mov r15,r1
.Lecopy:
mov.l @r3+,r0
mov.l r0,@r1
add #4,r1
dt r2
bf .Lecopy
.Lecall:
jsr @r8
nop
add #64,r15
lds.l @r15+,pr
ldc.l @r15+,gbr
mov.l @r15+,r8
rts
nop
.align 2
_phone_import:
mov.l r8,@-r15
stc.l gbr,@-r15
sts.l pr,@-r15
add #-80,r15
mov r0,r8
mov r1,r2
add #-4,r2
cmp/pl r2
bf .Licall
mov r15,r3
add #92,r3
mov r15,r1
add #16,r1
.Licopy:
mov.l @r3+,r0
mov.l r0,@r1
add #4,r1
dt r2
bf .Licopy
.Licall:
jsr @r8
nop
add #80,r15
lds.l @r15+,pr
ldc.l @r15+,gbr
mov.l @r15+,r8
rts
nop
'''
prototypes={};imports={};report=[];link=[]
for name,source,id_ in units:
    aux=(OBJ/(name+'.aux')).read_text();decls={}
    for match in re.finditer(r'\*/\s+extern\s+([^;]+?\b(\w+)\s*\((.*?)\));',aux):
        decls[match[2]]=(match[1],match[3])
    prototypes[name]=decls
    asm=[DISPATCH];calls={}
    for symbol,(prototype,params) in decls.items():
        if symbol.startswith('__nt_msvc_import_'):
            public=symbol[len('__nt_msvc_import_'):];n=arity(params);calls[public]=n
            asm += ['.align 2',f'.global _{symbol}',f'_{symbol}:', 'mov.l 1f,r0','mov.l @r0,r0',f'mov #{n},r1','mov.l 2f,r2','jmp @r2','nop','.align 2',f'1: .long ___imp_{public}','2: .long _phone_import']
    for symbol,(prototype,params) in decls.items():
        if symbol.startswith('__') or symbol=='main':continue
        try:n=arity(params)
        except ValueError:continue
        # Local helpers may have struct contracts; only public definitions are
        # selected for EAT publication by build_phone's exact surface manifest.
        asm += ['.align 2',f'.global _msvc_export_{symbol}',f'_msvc_export_{symbol}:','mov.l 1f,r0',f'mov #{n},r1','mov.l 2f,r2','jmp @r2','nop','.align 2',f'1: .long _{symbol}','2: .long _phone_export']
    if name not in ('core','calc','starthost') and not name.startswith('app-'):
        asm += ['.global _msvc_unsupported','_msvc_unsupported:','mov.l 1f,r0','mov #0,r1','mov.l 2f,r2','jmp @r2','nop','.align 2','1: .long _phone_unsupported','2: .long _phone_export']
    (OBJ/(name+'.S')).write_text('\n'.join(asm)+'\n')
    script='payload/payload.ld' if name in ('calc','starthost') or name.startswith('app-') else 'payload/xamlui.ld'
    link += [f'sh4a_nofpueb-elf-gcc -c obj/phone-msvc/{name}.S -o obj/phone-msvc/{name}.abi.o',f'sh4a_nofpueb-elf-g++ -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T {script} -o obj/phone-msvc/{name}.elf obj/phone-msvc/{name}.o obj/phone-msvc/{name}.abi.o obj/phone-msvc/tiny_memory.o -lgcc']
    report.append(dict(unit=name,import_words=calls))
run_script(link)
# Preserve the native phone core's ordinal/name surface, not the CE ordinal map.
import pefile
native=pefile.PE(str(ROOT/'dist/COREDLL.DLL'))
elf=pack.Elf((OBJ/'core.elf').read_bytes());defs={s.name:s.value for s in elf.symbols if s.shndx!=pack.SHN_UNDEF and s.shndx<pack.SHN_LORESERVE}
entries=[]
for e in native.DIRECTORY_ENTRY_EXPORT.symbols:
    name=e.name.decode() if e.name else None
    address=defs.get('_msvc_export_'+name,defs['_msvc_export_coredll_stub']) if name else defs['_msvc_export_coredll_stub']
    entries.append((e.ordinal,name,address-0x10000000))
pack.build_export_section=lambda _,n,r:phone.export_section(entries,n,r)
binary,_=pack.emit_ntx(elf,0x10000000,exports=[('surface',0)],dll_name='COREDLL.DLL',image_is_dll=True);(OUT/'COREDLL.DLL').write_bytes(binary)
elf=pack.Elf((OBJ/'calc.elf').read_bytes());binary,_=pack.emit_ntx(elf,0x400000,import_module='COREDLL.DLL');(OUT/'CALC.EXE').write_bytes(binary)
shutil.copyfile(ROOT/'dist/NTOS.hh3',OUT/'NTOS.hh3')
subprocess.run([sys.executable,str(HERE/'build_phone.py'),'--pack-only','--msvc','--out',relative(OUT),'--obj',relative(OBJ)],check=True,cwd=ROOT)
(OUT/'msvc-boundaries.json').write_text(json.dumps(report,indent=2))
(OUT/'README-ABI.txt').write_text('Experimental MSVC scalar PE export/import adapters. Private UI tables, callbacks and NTOS gates remain native. Not run or ABI-certified. Keep this whole bundle together; do not mix with native phone or CE-ordinal DLLs.\n')
print('BUILT isolated MSVC phone bundle:',OUT)



