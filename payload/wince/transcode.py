"""Conservative Ghidra-guided LE->BE PE candidate generator. Never swaps PE metadata.
Original addresses and instruction halfwords are preserved; executable entry adapters
only supply the Microsoft 16-byte argument home area and appropriate entry arguments.
"""
import argparse,hashlib,json,re,struct
from pathlib import Path
import pefile
ROOT=Path(__file__).resolve().parents[2]
def align(n,a):return (n+a-1)&~(a-1)
def append_section(binary,name,data,flags=0x60000020):
 b=bytearray(binary);pe=pefile.PE(data=b);coff=pe.DOS_HEADER.e_lfanew+4;opt=coff+20
 count=pe.FILE_HEADER.NumberOfSections;assert count<8
 table=opt+pe.FILE_HEADER.SizeOfOptionalHeader;old=pe.OPTIONAL_HEADER.SizeOfHeaders
 headers=align(table+40*(count+1),pe.OPTIONAL_HEADER.FileAlignment);shift=max(0,headers-old)
 if shift:
  b[old:old]=bytes(shift)
  for i in range(count):
   at=table+i*40+20;v=struct.unpack_from('<I',b,at)[0]
   if v:struct.pack_into('<I',b,at,v+shift)
 struct.pack_into('<I',b,opt+60,max(old,headers))
 va=align(pe.OPTIONAL_HEADER.SizeOfImage,pe.OPTIONAL_HEADER.SectionAlignment)
 raw=align(len(b),pe.OPTIONAL_HEADER.FileAlignment);size=align(len(data),pe.OPTIONAL_HEADER.FileAlignment)
 b.extend(bytes(raw-len(b)));b.extend(data);b.extend(bytes(size-len(data)))
 struct.pack_into('<8sIIIIIIHHI',b,table+40*count,name.encode().ljust(8,b'\0'),len(data),va,size,raw,0,0,0,0,flags)
 struct.pack_into('<H',b,coff+2,count+1);struct.pack_into('<I',b,opt+56,align(va+len(data),pe.OPTIONAL_HEADER.SectionAlignment))
 return bytes(b),va

def convert(source,inventory,entry_adapter=None):
 raw=source.read_bytes();manifest=json.loads((Path(__file__).parent/'abi'/(source.name.lower()+'.json')).read_text())
 assert hashlib.sha256(raw).hexdigest()==manifest['sha256'],'Reference does not match the audited CE binary'
 pe=pefile.PE(data=raw);base=pe.OPTIONAL_HEADER.ImageBase
 out=bytearray(raw);protected=set(range(pe.OPTIONAL_HEADER.SizeOfHeaders));units={};instructions={}
 def protect(rva,n):
  try:o=pe.get_offset_from_rva(rva)
  except pefile.PEFormatError:return
  protected.update(range(o,o+n))
 # File/PE directories retain little-endian structure headers on both targets.
 for i in [0,1,2,5,6,12]:
  d=pe.OPTIONAL_HEADER.DATA_DIRECTORY[i]
  if d.VirtualAddress:protect(d.VirtualAddress,d.Size)
 for dep in getattr(pe,'DIRECTORY_ENTRY_IMPORT',[]):
  for table in [dep.struct.OriginalFirstThunk,dep.struct.FirstThunk]:
   if table:protect(table,4*(len(dep.imports)+1))
  for imp in dep.imports:
   if imp.name:protect(imp.hint_name_table_rva,3+len(imp.name))
 for e in getattr(getattr(pe,'DIRECTORY_ENTRY_EXPORT',None),'symbols',[]):
  if e.name:protect(e.name_offset,0) if False else None
 def unit(va,n,kind):
  try:o=pe.get_offset_from_rva(va-base)
  except (pefile.PEFormatError,ValueError):return
  section=pe.get_section_by_rva(va-base)
  if section is None or va-base-section.VirtualAddress+n>section.SizeOfRawData:return
  if o<0 or o+n>len(raw) or any(x in protected for x in range(o,o+n)):return
  if kind=='I':instructions[o]=(n,va);units[o]=(n,kind);return
  if any(x in instructions for x in range(o-1,o+n)):return
  if kind=='L' or o not in units:units[o]=(n,kind)
 lines=inventory.read_text().splitlines()
 assert int(lines[0].split('\t')[1],16)==base,'Inventory base mismatch'
 # Instructions first, so inferred data can never overwrite decoded opcodes.
 for line in lines:
  p=line.split('\t',3)
  if p[0]=='I':
   assert int(p[2])==2,(source,p)
   unit(int(p[1],16),2,'I')
 for line in lines:
  p=line.split('\t',3)
  if p[0]=='L':unit(int(p[1],16),int(p[2]),'L')
  if p[0]=='D':
   n=int(p[2]);ty=p[3].lower();va=int(p[1],16)
   if n in [2,4,8] and not any(t in ty for t in ['string','char','byte','image_']):unit(va,n,'D')
   if 'unicode' in ty:
    for off in range(0,n-1,2):unit(va+off,2,'U')
 for block in getattr(pe,'DIRECTORY_ENTRY_BASERELOC',[]):
  for e in block.entries:
   if e.type==3:unit(base+e.rva,4,'L')
 # .pdata consists of native 32-bit CE exception records, not PE directories.
 for sec in pe.sections:
  if sec.Name.rstrip(b'\0')==b'.pdata':
   for off in range(0,sec.Misc_VirtualSize,4):unit(base+sec.VirtualAddress+off,4,'D')
 # Manually verified UTF-16 literal not typed by Ghidra (shell console suffix).
 if source.name.lower()=='shell.exe':
  for va in range(base+0x7648,base+0x7650,2):unit(va,2,'U')
  # 25 verified command records: WCHAR name[32], handler pointer, flags.
  for i in range(25):
   at=base+0x1354+72*i
   for off in range(0,64,2):unit(at+off,2,'U')
   unit(at+64,4,'D');unit(at+68,4,'D')
 used=set();changes=[]
 # Prioritize code and literal pools over inferred data; fail on ambiguous overlap.
 for off,(n,kind) in sorted(units.items(),key=lambda u:({'I':0,'L':1,'U':2,'D':3}[u[1][1]],u[0])):
  if any(x in used for x in range(off,off+n)):continue
  out[off:off+n]=raw[off:off+n][::-1];used.update(range(off,off+n));changes.append((off,n,kind))
 for off,(n,va) in instructions.items():assert out[off:off+2]==raw[off:off+2][::-1]
 struct.pack_into('<H',out,pe.DOS_HEADER.e_lfanew+4,0x1a8)
 # Remove ROM/debug directories that reference obsolete file offsets/tooling.
 opt=pe.DOS_HEADER.e_lfanew+24
 struct.pack_into('<II',out,opt+96+6*8,0,0)
 if entry_adapter:
  out,new_entry=append_section(out,'.beinit',entry_adapter)
  out=bytearray(out);struct.pack_into('<I',out,opt+16,new_entry)
  if source.name.lower()=='toolhelp.dll':
   # The DLL is relocatable; its appended entry literal must relocate too.
   old=pe.OPTIONAL_HEADER.DATA_DIRECTORY[5]
   data=raw[pe.get_offset_from_rva(old.VirtualAddress):pe.get_offset_from_rva(old.VirtualAddress)+old.Size]
   literal=new_entry+entry_adapter.index(struct.pack('>I',base+pe.OPTIONAL_HEADER.AddressOfEntryPoint))
   data+=struct.pack('<IIHH',literal&~4095,12,0x3000|(literal&4095),0)
   out,reloc_rva=append_section(out,'.bereloc',data,0x42000040)
   out=bytearray(out);struct.pack_into('<II',out,opt+96+5*8,reloc_rva,len(data))
 report={'source':source.name,'source_sha256':hashlib.sha256(raw).hexdigest(),'instructions':len(instructions),'instruction_changes':'16-bit byte order only','data_units':len(changes)-len(instructions),'rewritten_units':changes,'preserved_preferred_base':hex(base),'entry_adapter':bool(entry_adapter),'full_runtime_verified':False}
 return bytes(out),report
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('--reference',type=Path,required=True);a.add_argument('--inventory',type=Path,required=True);a.add_argument('--out',type=Path,default=ROOT/'dist/ce-demo');args=a.parse_args();args.out.mkdir(parents=True,exist_ok=True)
 for name in ['shell.exe','control.exe','toolhelp.dll','commctrl.dll']:
  adapter=ROOT/'obj/wince'/(name+'.entry.bin');b,report=convert(args.reference/name,args.inventory/(name+'.tsv'),adapter.read_bytes() if adapter.exists() else None)
  output=args.out/(name if name!='commctrl.dll' else 'commctrl.full-candidate.dll');output.write_bytes(b)
  (output.with_suffix(output.suffix+'.audit.json')).write_text(json.dumps(report,indent=2)+'\n')
  print(name,'->',output.name,len(b),'bytes;',report['instructions'],'preserved instruction halfwords')




