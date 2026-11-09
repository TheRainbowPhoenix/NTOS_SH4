"""Execute the six original integer-only MSVC probe bodies in BE clemu.
Only this deliberately pool-free .text is converted by instruction halfword;
this is NOT a generic COFF/endian converter and performs no ABI translation.
"""
import json,struct,re
from pathlib import Path
import clemu.core as c
R=Path(__file__).resolve().parents[2];O=R/'obj_ce_abi/msvc-rules';BASE=0x00400000;STACK=0x0c3ff000;STOP=0x0c020000;BUFFER=0x0c030000

def coff(path):
 b=path.read_bytes();machine,n,stamp,symoff,nsyms,opts,flags=struct.unpack_from('<HHIIIHH',b)
 sections=[]
 for i in range(n):
  at=20+opts+40*i;name=b[at:at+8].split(b'\0')[0].decode();size,raw=struct.unpack_from('<II',b,at+16)
  sections.append((name,b[raw:raw+size]))
 textindex=next(i+1 for i,s in enumerate(sections) if s[0]=='.text');text=sections[textindex-1][1]
 strings=symoff+nsyms*18;symbols={};i=0
 while i<nsyms:
  at=symoff+18*i;name,value,section,ty,storage,aux=struct.unpack_from('<8sIhHBB',b,at)
  if name[:4]==b'\0'*4:
   p=strings+struct.unpack_from('<I',name,4)[0];name=b[p:b.index(b'\0',p)]
  if section==textindex:symbols[name.split(b'\0')[0].decode()]=BASE+value
  i+=1+aux
 return machine,text,symbols

results=[]
for name in ['default-Od','default-O2','sh4-Od','sh4-O2']:
 asm=(O/(name+'.asm')).read_text();assert not re.search(r'\.data\.[wl]',asm),'Pool data requires typed conversion'
 machine,text,symbols=coff(O/(name+'.obj'));assert len(text)%2==0
 text=b''.join(text[i:i+2][::-1] for i in range(0,len(text),2))
 emu=c.Emulator(c.FunctionTable.empty());disp=c.HeadlessDisplayModule().add_to(emu);touch=c.TouchModule(0,0,1,1).add_to(emu)
 c.RamDevice(BASE,0x10000,'probe').map_to(emu);c.RamDevice(0x0c000000,0x400000,'scratch').map_to(emu)
 def stop(m):m.cpu.regs.stop_cpu=True;m.cpu.regs.pc=STOP-2;return False
 emu.add_breakpoint(STOP,stop);emu.setup();emu.lock(lambda m:m.write_bulk(BASE,text));emu.lock(lambda m:m.write_bulk(STOP-2,b'\x00\x09\x00\x09'))
 saved={i:0x12800000+0x101*i for i in range(8,15)}
 def call(fn,args,overflow=(),wrong_slot=False):
  got={}
  def start(m):
   r=m.cpu.regs;r.sr=0x40000000;r.pc=symbols['_'+fn];r.pr=STOP;r.r15=STACK;r.stop_cpu=False
   m.write_bulk(STACK,struct.pack('>12I',*([0xa5a5a5a5]*12)))
   for i,v in saved.items():r.set_r(i,v)
   for i in range(4):r.set_r(i+4,args[i] if i<len(args) else 0)
   for i,v in enumerate(overflow):m.write_bulk(STACK+16+4*i,struct.pack('>I',v))
   if wrong_slot:m.write_bulk(STACK,struct.pack('>I',5))
  emu.lock(start);emu.run(disp.disp,touch.touch,max_insts=20000)
  def finish(m):
   r=m.cpu.regs;got.update(pc=r.pc,result=r.r0,sp=r.r15,pr=r.pr,preserved={i:r.get_r(i) for i in saved},buffer=m.read_bulk(BUFFER,12).hex())
  emu.lock(finish)
  assert got['pc']==STOP and got['sp']==STACK and got['pr']==STOP,got
  assert got['preserved']==saved,got
  return got
 tests=[('abi_1',[100],[],101),('abi_4',[1,2,4,8],[],15),('abi_5',[1,2,3,4],[5],15),('abi_8',[1,2,3,4],[5,6,7,8],36),('abi_caller',[100],[],114)]
 for fn,args,overflow,expected in tests:
  got=call(fn,args,overflow);assert got['result']==expected,(name,fn,got)
  results.append({'build':name,'function':fn,'result':expected,'sp_pr_and_r8_r14_restored':True})
 got=call('abi_struct',[BUFFER,0x12345678,0x55]);assert got['result']==BUFFER
 assert got['buffer']==struct.pack('>III',0x12345678,0x55,0x12345678^0x55).hex(),got
 results.append({'build':name,'function':'abi_struct','hidden_buffer':'R4','x':'R5','y':'R6','return':'R0=buffer','sp_pr_and_r8_r14_restored':True})
 bad=call('abi_5',[1,2,3,4],wrong_slot=True);assert bad['result']!=15
 results.append({'build':name,'negative_control':'argument 5 incorrectly at SP+0','mismatch_detected':True})
 print('PASS',name,'machine',hex(machine),'6 functions + negative control')
(O/'runtime.json').write_text(json.dumps({'method':'original COFF pool-free integer code, instruction byte order changed only','calls':results},indent=2),encoding='utf-8')
