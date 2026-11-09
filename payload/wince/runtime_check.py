"""Run actual BE machine code in clemu; stop at unsupported CE services."""
import sys,struct,json
from pathlib import Path
import pefile,clemu.core as c
D=Path(__file__).resolve().parents[2]/'dist/ce-demo'
E=c.Emulator(c.FunctionTable.empty());display=c.HeadlessDisplayModule().add_to(E);touch=c.TouchModule(0,0,1,1).add_to(E)
mods={};heap=0x0c100000;output=[];unsupported=[]
for addr,size in [(0x0c000000,0x400000),(0x08100000,0x4000)]:c.RamDevice(addr,size,'scratch').map_to(E)
def w(m,a,b):m.write_bulk(a,b)
def u(m,a):return int.from_bytes(m.read_bulk(a,4),'big')
def put(m,a,v):w(m,a,struct.pack('>I',v&0xffffffff))
def string(m,a):
 b=bytearray()
 for i in range(4096):
  x=m.read_bulk(a+i,1)[0]
  if not x:break
  b.append(x)
 return b.decode('ascii','replace')
for name in ['COREDLL.DLL','toolhelp.dll','COMMCTRL.DLL',('control.exe' if '--control' in sys.argv else 'shell.exe')]:
 p=pefile.PE(str(D/name));base=p.OPTIONAL_HEADER.ImageBase
 if name=='COMMCTRL.DLL':base=0x11000000
 if '--ntos-bases' in sys.argv:
  base={'COREDLL.DLL':0x01000000,'toolhelp.dll':0x02000000,'COMMCTRL.DLL':0x03000000}.get(name,base)
 image=bytearray(p.get_memory_mapped_image());image.extend(bytes(p.OPTIONAL_HEADER.SizeOfImage-len(image)))
 delta=base-p.OPTIONAL_HEADER.ImageBase
 if delta:
  for block in getattr(p,'DIRECTORY_ENTRY_BASERELOC',[]):
   for r in block.entries:
    if r.type==3:struct.pack_into('>I',image,r.rva,(struct.unpack_from('>I',image,r.rva)[0]+delta)&0xffffffff)
 # Bus translates SH P1/P2 and P3 aliases through physical mask.
 c.RamDevice(base&0x1fffffff,len(image),name).map_to(E)
 E.lock(lambda m,base=base,image=image:m.write_bulk(base,bytes(image)))
 exports={}
 for x in getattr(getattr(p,'DIRECTORY_ENTRY_EXPORT',None),'symbols',[]):
  exports[x.ordinal]=base+x.address
  if x.name:exports[x.name.decode()]=base+x.address
 mods[name.lower()]=(p,base,exports)
STOP=0x0c020000
def stop(m):
 m.cpu.regs.stop_cpu=True
 m.cpu.regs.pc=STOP-2
 return False
E.add_breakpoint(STOP,stop)
def gate(i):
 def f(m):
  global heap
  r=m.cpu.regs;v=0
  if i==0:
   s=string(m,r.r4);output.append(s);print('OUT',s,flush=True)
  elif i==3:v=heap;heap=(heap+r.r4+15)&~15
  elif i==5:v=2
  elif i==15:v=2
  elif i>=16:v=0xfffffff2
  r.r0=v;r.pc=r.pr;return False
 return f
for i in range(28):E.add_breakpoint(0x0c010000+i*4,gate(i))
def init(m):
 w(m,STOP-2,b'\x00\x09\x00\x09')
 put(m,0x08100000,0x0c001000)
 for i,v in enumerate([0x4e415049,1,28],1):put(m,0x08100000+4*i,v)
 for i in range(28):put(m,0x0c001000+4*i,0x0c010000+i*4)
 for p,base,exports in mods.values():
  for imp in getattr(p,'DIRECTORY_ENTRY_IMPORT',[]):
   target=mods[imp.dll.decode().lower()][2]
   for x in imp.imports:
    key=x.name.decode() if x.name else x.ordinal
    put(m,base+x.address-p.OPTIONAL_HEADER.ImageBase,target[key])
E.setup();E.lock(init)
# Export stub entry records its requested ordinal in r4. Trap before it returns failure.
core=mods['coredll.dll'][2]
def fail(m):unsupported.append(m.cpu.regs.r4);print('UNSUPPORTED',unsupported[-1]);m.cpu.regs.stop_cpu=True;return False
# All unsupported ordinal thunks jump to the same C handler: locate from ELF.
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'));import elf2ntx
elf=elf2ntx.Elf((D.parents[1]/'obj/wince/core.elf').read_bytes())
addr=next(s.value for s in elf.symbols if s.name=='_CeUnsupported')+mods['coredll.dll'][1]-0x10000000;E.add_breakpoint(addr,fail)
def call(addr,args=(),stack=(),limit=2000000):
 result={}
 def start(m):
  r=m.cpu.regs;r.sr=0x40000000;r.pc=addr;r.pr=STOP;r.r15=0x0c3ff000;r.stop_cpu=False
  for i in range(4):r.set_r(4+i,args[i] if i<len(args) else 0)
  for i,v in enumerate(stack):put(m,r.r15+16+i*4,v)
 E.lock(start);E.run(display.disp,touch.touch,max_insts=limit)
 def end(m):result.update(pc=m.cpu.regs.pc,r0=m.cpu.regs.r0)
 E.lock(end)
 if result['pc']!=STOP:raise RuntimeError('did not return: '+str(result)+' unsupported='+str(unsupported))
 return result['r0']
assert call(core['coredll_abi5'],[1,2,3,4],[5])==0xc0de0069
results={'layout':'ntos-low' if '--ntos-bases' in sys.argv else 'preferred'}
for name in ['coredll.dll','toolhelp.dll','commctrl.dll']:
 p,b,x=mods[name];results[name+'.entry']=call(b+p.OPTIONAL_HEADER.AddressOfEntryPoint,[b,1,0])
# Real ordinal 506 with a fifth stack argument and deliberate invalid process.
SRC=0x0c030000;DST=SRC+64;DONE=DST+64
# MSVC variadic home slots and two additional stack words.
fmt='%d/%d/%d/%d'
E.lock(lambda m:w(m,SRC,fmt.encode('utf-16-be')+b'\0\0'))
assert call(core[56],[DST,SRC,11,22],[33,44])==11
E.lock(lambda m:results.update(varargs=m.read_bulk(DST,24).decode('utf-16-be').rstrip('\0')))
assert results['varargs']=='11/22/33/44'
E.lock(lambda m:w(m,SRC,b'CE-BE ABI test\0'))
assert call(core[506],[1,SRC,DST,15],[DONE])==1
E.lock(lambda m:results.update(copied=m.read_bulk(DST,15).hex(),bytes_read=u(m,DONE)))
assert results['bytes_read']==15 and results['copied']==b'CE-BE ABI test\0'.hex()
assert call(core[506],[2,SRC,DST,15],[DONE])==0
allocated=call(core[33],[0x40,16]);assert allocated
E.lock(lambda m:w(m,allocated,b'LOCAL TEST'+bytes(6)))
resized=call(core[34],[allocated,32,0x42]);assert resized and call(core[35],[resized])==32
E.lock(lambda m:results.update(local_memory=m.read_bulk(resized,32).hex()))
assert bytes.fromhex(results['local_memory'])[:10]==b'LOCAL TEST'
assert bytes.fromhex(results['local_memory'])[16:]==bytes(16)
assert call(core[36],[resized])==0
th=mods['toolhelp.dll'][2]
assert call(th['Toolhelp32ReadProcessMemory'],[0,SRC,DST,15],[DONE])==1
E.lock(lambda m:results.update(toolhelp_read_count=u(m,DONE)))
assert results['toolhelp_read_count']==15
assert call(th['Toolhelp32ReadProcessMemory'],[2,SRC,DST,15],[DONE])==0
snap=call(th['CreateToolhelp32Snapshot'],[14,0]);assert snap not in (0,0xffffffff)
for fn,size in [('Process32First',0x234),('Module32First',0x434),('Thread32First',0x24)]:
 E.lock(lambda m,size=size:put(m,DST,size));results[fn]=call(th[fn],[snap,DST]);assert results[fn]==1
E.lock(lambda m:put(m,DST,0x234))
assert call(th['Process32Next'],[snap,DST])==0
assert call(core[516])==18
results['close_snapshot']=call(th['CloseToolhelp32Snapshot'],[snap])
cc=mods['commctrl.dll'][2]
E.lock(lambda m:w(m,DST,struct.pack('>II',8,0x40)))
assert call(cc[2],[DST])==1
bar=call(cc[3],[0,1,42]);assert bar
assert call(cc[12],[bar])==26
assert call(cc[8],[bar,0,1,0])==1
assert call(cc[10],[bar,0,0])==1
assert call(cc[44],[bar])==1
results['commctrl_six_reference_ordinals']=True
(D/'runtime-results.json').write_text(json.dumps(results,indent=2));print('PASS',results)
if '--shell' in sys.argv:
 seen=set()
 for name,addr in core.items():
  if isinstance(name,str):
   def trace(m,name=name):
    if name not in seen: print('API',name,hex(m.cpu.regs.r4),m.read_bulk(m.cpu.regs.r4,64).hex() if name=='CreateFileW' else '',flush=True);seen.add(name)
    return False
   E.add_breakpoint(addr,trace)
 p,b,_=mods['shell.exe'];result=call(b+p.OPTIONAL_HEADER.AddressOfEntryPoint,limit=5000000);assert result==1
 text=''.join(output);assert 'Welcome to the Windows CE Shell' in text and 'Unknown command' not in text and 'priority' in text
 (D/'shell-output.txt').write_text(text)
 (D/('shell-runtime-low.json' if '--ntos-bases' in sys.argv else 'shell-runtime.json')).write_text(json.dumps({'return':result,'original_help':True,'unsupported':unsupported},indent=2));print('SHELL PASS',result)




if '--control' in sys.argv:
 p,b,_=mods['control.exe']
 calls=[]
 for name,addr in core.items():
  if isinstance(name,str):
   def trace_control(m,name=name):
    if name not in calls:calls.append(name);print('CONTROL API',name,flush=True)
    return False
   E.add_breakpoint(addr,trace_control)
 try:
  result=call(b+p.OPTIONAL_HEADER.AddressOfEntryPoint,limit=500000)
  assert all(name in calls for name in ['RegisterClassW','CreateWindowExW','ShowWindow','UpdateWindow','GetMessageW'])
  live={}
  def inspect_controls(m):
   offset=mods['coredll.dll'][1]-0x10000000
   symbol=next(s.value for s in elf.symbols if s.name=='_ce_cc_windows')+offset
   current=u(m,symbol);kinds=[]
   while current and len(kinds)<64:kinds.append(u(m,current+4));current=u(m,current)
   live['control_kinds']=kinds
  E.lock(inspect_controls)
  assert 1 in live['control_kinds'],live
  (D/'control-runtime.json').write_text(json.dumps({'status':'window-and-message-loop','return':result,'import_snap':'resolved','unsupported':unsupported,'apis':calls,'live':live,'visual_verified':False},indent=2))
  print('CONTROL RESULT',result)

 except RuntimeError:
  (D/'control-runtime.json').write_text(json.dumps({'status':'incomplete','import_snap':'resolved','unsupported':unsupported,'first_missing':'LoadCursorW' if unsupported==[683] else None},indent=2))
  raise










# Match NTOS DLL detach order and prove that profile-owned object registries clear.
for name in ['commctrl.dll','toolhelp.dll','coredll.dll']:
 p,b,_=mods[name];assert call(b+p.OPTIONAL_HEADER.AddressOfEntryPoint,[b,0,0])==1
cleared={}
def inspect_detach(m):
 for name in ['_ce_cc_windows','_ce_images','_ce_locals','_ce_resource_strings']:
  symbol=next(s.value for s in elf.symbols if s.name==name)+mods['coredll.dll'][1]-0x10000000
  cleared[name]=u(m,symbol)
E.lock(inspect_detach);assert not any(cleared.values()),cleared
(D/'detach-runtime.json').write_text(json.dumps(cleared,indent=2));print('DLL DETACH PASS')
