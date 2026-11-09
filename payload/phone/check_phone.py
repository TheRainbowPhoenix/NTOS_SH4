#!/usr/bin/env python3
"""Independent finished-PE ABI, import and BE relocation checks (pefile)."""
import argparse,json,struct
from pathlib import Path
import pefile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--out',default='dist');args=ap.parse_args()
out=ROOT/args.out
modules=['STARTHOST.EXE','STARTMENU.DLL','UIXMOBILE.DLL','UIXN.DLL','UIXRENDER.DLL','SHELLCONTROLS.DLL','SHELLCHROME.DLL','COREDLL.DLL']
images={n:pefile.PE(str(out/n)) for n in modules}
total=0
for n,pe in images.items():
 assert pe.FILE_HEADER.Machine==0x1a8,(n,'SH4 BE')
 assert pe.FILE_HEADER.NumberOfSections<=8
 assert pe.OPTIONAL_HEADER.Subsystem==9
 if n not in ['STARTHOST.EXE','COREDLL.DLL']:
  assert pe.OPTIONAL_HEADER.ImageBase==0x10000000
  reference=json.loads((HERE/'abi'/(n.lower()+'.json')).read_text())
  actual={(e.ordinal,e.name.decode() if e.name else None) for e in pe.DIRECTORY_ENTRY_EXPORT.symbols}
  expected={(e['ordinal'],e['name']) for e in reference['exports']}
  assert actual==expected,(n,'export names/ordinals differ',actual^expected)
 for dep in getattr(pe,'DIRECTORY_ENTRY_IMPORT',[]):
  target=images[dep.dll.decode().upper()]
  names={e.name for e in target.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
  for imp in dep.imports:assert imp.name in names,(n,dep.dll,imp.name)
 # Rebase every image to a different address, using BE literal values.
 memory=bytearray(pe.get_memory_mapped_image());delta=0x01000000-pe.OPTIONAL_HEADER.ImageBase
 count=0
 for block in pe.DIRECTORY_ENTRY_BASERELOC:
  for e in block.entries:
   if e.type==0:continue
   assert e.type==3
   before=struct.unpack_from('>I',memory,e.rva)[0]
   assert pe.OPTIONAL_HEADER.ImageBase<=before<pe.OPTIONAL_HEADER.ImageBase+pe.OPTIONAL_HEADER.SizeOfImage,(n,hex(e.rva),hex(before))
   struct.pack_into('>I',memory,e.rva,(before+delta)&0xffffffff)
   assert struct.unpack_from('>I',memory,e.rva)[0]==(before+delta)&0xffffffff
   count+=1
 assert count>0,(n,'missing real relocations')
 total+=pe.OPTIONAL_HEADER.SizeOfImage
 print(n,len(pe.__data__),'bytes',count,'BE relocations')
host=images['STARTHOST.EXE']
deps={d.dll.decode().upper():[i.name.decode() for i in d.imports] for d in host.DIRECTORY_ENTRY_IMPORT}
assert deps['UIXMOBILE.DLL']==['RunApplication']
assert deps['STARTMENU.DLL']==['StartMenuInitialize','StartMenuUninitialize']
assert total<256*1024,('resident module image budget exceeded',total)
print('PASS exact six DLL export surfaces, all imports resolved, SH4 BE relocations; mapped images',total,'bytes')
