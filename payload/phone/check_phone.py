#!/usr/bin/env python3
"""Independent finished-PE ABI, import and BE relocation checks (pefile)."""
import argparse,json,struct
from pathlib import Path
import pefile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--out',default='dist');args=ap.parse_args()
out=ROOT/args.out
modules=['STARTHOST.EXE','STARTMENU.DLL','UIXMOBILE.DLL','UIXN.DLL','UIXRENDER.DLL','SHELLCONTROLS.DLL','SHELLCHROME.DLL','FINGERKB.DLL','SVF.DLL','APPCATALOG.DLL','SETTINGSUI.DLL','COREDLL.DLL']
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
stubs=list((out/'Apps').glob('*/*.exe'))
assert len(stubs)==24
for path in stubs:
 app=pefile.PE(str(path));assert app.FILE_HEADER.Machine==0x1a8
 assert app.FILE_HEADER.NumberOfSections<=8
 assert app.OPTIONAL_HEADER.DATA_DIRECTORY[2].VirtualAddress
 data=app.DIRECTORY_ENTRY_RESOURCE.entries[0].directory.entries[0].directory.entries[0].data.struct
 assert app.get_data(data.OffsetToData,data.Size).startswith((b'VEC1',b'VEC2'))
 for dep in app.DIRECTORY_ENTRY_IMPORT:
  names={e.name for e in images[dep.dll.decode().upper()].DIRECTORY_ENTRY_EXPORT.symbols if e.name}
  assert all(i.name in names for i in dep.imports)
 meta=path.with_name(path.name+'.manifest').read_bytes();assert meta[:4]==b'PAM1'
 assert len(meta)==16+meta[12]+int.from_bytes(meta[14:16],'big')
pending=['STARTHOST.EXE'];active=set();pages=0
while pending:
 name=pending.pop()
 if name in active:continue
 active.add(name);image=images[name];pages+=(image.OPTIONAL_HEADER.SizeOfImage+4095)//4096
 pending.extend(d.dll.decode().upper() for d in getattr(image,'DIRECTORY_ENTRY_IMPORT',[]))
assert len(active)<=16 and pages<=55,('resident loader budget exceeded',len(active),pages)
print('PASS ten DLL export surfaces, all imports resolved, SH4 BE relocations, 24 embedded-resource apps; active graph',pages,'pages; mapped images',total,'bytes')

# Independently decode the portable cache instead of assuming C struct layout.
cache=(out/'apps.dat').read_bytes();assert cache[:4]==b'CAT1' and cache[6:8]==b'\x01\0'
assert int.from_bytes(cache[4:6],'big')==24
pos=8;ids=set()
for _ in range(24):
 id_,flags,size,color,n,p,v,off,sz,kind=struct.unpack_from('>HBBIBBHIIB3x',cache,pos);pos+=24
 name=cache[pos:pos+n].decode();pos+=n
 path=cache[pos:pos+p].decode();pos+=p
 vector=cache[pos:pos+v];pos+=v
 assert id_ not in ids and size in (1,2) and vector.startswith((b'VEC1',b'VEC2'));ids.add(id_)
 assert path.startswith('\\fls0\\Apps\\') and not path.startswith('\\\\')
 exe=out/Path(path[len('\\fls0\\'):].replace('\\','/'))
 assert exe.is_file() and exe.with_suffix('.svf').read_bytes()==vector
assert pos==len(cache) and ids==set(range(1,25))
print('PASS compact app cache:',len(cache),'bytes, 24 names/paths/SVF icons')
