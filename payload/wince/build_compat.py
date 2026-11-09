#!/usr/bin/env python3
"""Build the isolated CE ordinal/ABI profile, never the phone COREDLL."""
import sys,json,subprocess,importlib.util,struct
from msvc_boundary import generate,upstream
from pathlib import Path
R=Path(__file__).resolve().parents[2]; H=Path(__file__).resolve().parent; O=R/'obj/wince'; D=R/'dist/ce-demo'
sys.path.insert(0,str(R/'payload/phone'))
from build_phone import export_section
spec=importlib.util.spec_from_file_location('pack',R/'tools/elf2ntx.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
def run(s): subprocess.run(["docker","run","--rm","-v",str(R)+":/work","-w","/work","ghcr.io/classpaddev/hollyhock-3:v2.2.1","sh","-c","export PATH=/toolchain/bin:$PATH; "+s],check=True,cwd=R)
def symbols(path):
 e=p.Elf(path.read_bytes());return {s.name:s.value for s in e.symbols if s.shndx!=p.SHN_UNDEF and s.shndx<p.SHN_LORESERVE}
O.mkdir(parents=True,exist_ok=True);D.mkdir(parents=True,exist_ok=True)
(O/'msvc-call.S').write_text(upstream.call_asm())
run('sh4a_nofpueb-elf-gcc -c obj/wince/msvc-call.S -o obj/wince/msvc-call.o')
run('sh4a_nofpueb-elf-gcc -c payload/coredll_msvc_probe.c -o obj/wince/msvc-probe.o -Os -ffreestanding')
run('sh4a_nofpueb-elf-gcc -c payload/phone/tiny_memory.c -o obj/wince/memory.o -Os -ffreestanding -fno-builtin')
for stem,source,module in [('core','coredll_ce','COREDLL.DLL'),('commctrl','commctrl_ce','COMMCTRL.DLL')]:
 run(f'sh4a_nofpueb-elf-gcc -c payload/wince/{source}.c -o obj/wince/{stem}.o -Os -Wall -Wextra -Werror -Wno-unused-function -ffreestanding -fno-builtin -fno-stack-protector -Ipayload')
 run(f'sh4a_nofpueb-elf-g++ -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/coredll.ld -o obj/wince/{stem}.pre.elf obj/wince/{stem}.o obj/wince/memory.o obj/wince/msvc-call.o obj/wince/msvc-probe.o -lgcc')
 defs=symbols(O/f'{stem}.pre.elf'); manifest=json.loads((H/'abi'/module.lower()).with_suffix('.dll.json').read_text())
 signatures=json.loads((H/(stem+'_msvc_exports.json')).read_text())
 surface=list(manifest['exports'])
 if stem=='core':surface.extend({'ordinal':e['ordinal'],'name':e['name']} for e in signatures if e['ordinal']>=4090)
 lines,implemented=generate(signatures,surface,defs,stem)
 (O/f'{stem}.abi.S').write_text(lines)
 run(f'sh4a_nofpueb-elf-gcc -c obj/wince/{stem}.abi.S -o obj/wince/{stem}.abi.o')
 run(f'sh4a_nofpueb-elf-g++ -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/coredll.ld -o obj/wince/{stem}.elf obj/wince/{stem}.o obj/wince/{stem}.abi.o obj/wince/memory.o obj/wince/msvc-call.o obj/wince/msvc-probe.o -lgcc')
 elf=p.Elf((O/f'{stem}.elf').read_bytes());defs=symbols(O/f'{stem}.elf')
 entries=[(o,n,defs[f'_ce_export_{o}']-0x10000000) for o,n,_ in implemented]
 if stem=='core':
  for o,n in enumerate(['coredll_puts','CeFailure','coredll_malloc','coredll_free','CeCreateControl','CeControlMessage','CeDestroyControl','CeControlKind','CeControlSetBounds','CeControlGetBounds','CeDrawStatus'],4000):entries.append((o,{'coredll_puts':'CeNativePuts','coredll_malloc':'CeNativeAlloc','coredll_free':'CeNativeFree'}.get(n,n),defs['_'+n]-0x10000000))
 p.build_export_section=lambda _,n,r,entries=entries:export_section(entries,n,r)
 binary,relocs=p.emit_ntx(elf,0x10000000,exports=[('surface',0)],dll_name=module,image_is_dll=True,import_module='COREDLL.DLL' if p.collect_import_slots(elf) else None)
 (D/module).write_bytes(binary)
 (D/(module+'.profile.json')).write_text(json.dumps({'module':module,'implemented':[n for o,n,ok in implemented if ok],'unsupported':[{'ordinal':o,'name':n} for o,n,ok in implemented if not ok]},indent=2))
 print(module,len(binary),'bytes;',sum(ok for _,_,ok in implemented),'implemented; unsupported exports are explicit')




# Entry adapters use GCC only at the loader boundary; original CE calls stay CE ABI.
for name in ['shell.exe','control.exe','toolhelp.dll','commctrl.dll']:
 m=json.loads((H/'abi'/(name+'.json')).read_text());base=m['base'];entry=base+m['entry']
 a=['.text','.align 2','sts.l pr,@-r15','add #-16,r15']
 if name=='shell.exe':a+=['mov #0,r4']
 if name=='control.exe':a+=['mov.l 2f,r4','mov #0,r5','mov #0,r6','mov #1,r7']
 a+=['mov.l 1f,r0','jsr @r0','nop','add #16,r15','lds.l @r15+,pr','rts','nop','.align 2',f'1: .long {entry}',f'2: .long {base}']
 (O/(name+'.entry.S')).write_text('\n'.join(a)+'\n')
 run(f'sh4a_nofpueb-elf-gcc -c obj/wince/{name}.entry.S -o obj/wince/{name}.entry.o')
 run(f'sh4a_nofpueb-elf-objcopy -O binary -j .text obj/wince/{name}.entry.o obj/wince/{name}.entry.bin')

# Import-free sample CPL, discovered under the Windows directory.
run('sh4a_nofpueb-elf-gcc -c payload/wince/demo/demo_cpl.c -o obj/wince/demo.o -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin')
run('sh4a_nofpueb-elf-g++ -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/coredll.ld -o obj/wince/demo.pre.elf obj/wince/demo.o -lgcc')
signature=[dict(ordinal=1,name='CPlApplet',target='DemoCPlApplet',ret='u32',args=['ptr','u32','u32','ptr'],abi='fixed-words')]
assembly,_=generate(signature,[dict(ordinal=1,name='CPlApplet')],symbols(O/'demo.pre.elf'),'demo')
(O/'demo.abi.S').write_text(assembly)
run('sh4a_nofpueb-elf-gcc -c obj/wince/demo.abi.S -o obj/wince/demo.abi.o')
run('sh4a_nofpueb-elf-g++ -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/coredll.ld -o obj/wince/demo.elf obj/wince/demo.o obj/wince/demo.abi.o -lgcc')
elf=p.Elf((O/'demo.elf').read_bytes());defs=symbols(O/'demo.elf')
entries=[(1,'CPlApplet',defs['_ce_export_1']-0x10000000)]
p.build_export_section=lambda _,n,r:export_section(entries,n,r)
binary,_=p.emit_ntx(elf,0x10000000,exports=[('surface',0)],dll_name='demo.cpl',image_is_dll=True)
(D/'Windows').mkdir(exist_ok=True);(D/'Windows/demo.cpl').write_bytes(binary)
print('Windows/demo.cpl',len(binary),'bytes')
