#!/usr/bin/env python3
"""Full firmware/SDK/NTOS launch using ntos_launch.py and an isolated Fugue VFS."""
import argparse,sys,struct,json,time,shutil,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
EMU=Path(r'C:/Users/Phoebe/Documents/git/clemu-310c')
sys.path[:0]=[str(EMU),str(EMU/'clemu/scripts'),str(ROOT/'tools')]
import ntos_launch as launch
from fugue_hook import VirtualFilesystem, FileSystem
from harness import kiudata_from_keys
from PIL import Image
import elf2ntx

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,default=ROOT/'obj/wince/full-smoke');ap.add_argument('--app',choices=['control','shell','abi'],default='control');ap.add_argument('--direct-hh3',action='store_true');ap.add_argument('--capture-after',type=int,default=20_000_000);ap.add_argument('--budget',type=int,default=600_000_000);args=ap.parse_args();out=args.out;out.mkdir(parents=True,exist_ok=True)
 volume=out/'vfs/fls0';volume.mkdir(parents=True,exist_ok=True)
 chosen={'control':ROOT/'dist_ce_demo/control.exe','shell':ROOT/'dist_ce_demo/shell.exe','abi':ROOT/'dist_msvc_coredll/COREDLLABI.EXE'}[args.app]
 for path in [ROOT/'dist/NTOS.hh3',ROOT/'dist_ce_demo/COREDLL.DLL',ROOT/'dist_ce_demo/COMMCTRL.DLL',ROOT/'dist_ce_demo/toolhelp.dll',chosen,launch.DEFAULT_RUN_BIN]:shutil.copyfile(path,volume/path.name)
 modules,emu=launch.make_emulator(EMU/'refs/fw3070.bin')
 vfs=VirtualFilesystem(out/'vfs');vfs.mounts.append(('fls0',FileSystem(volume)));launch.install_os_hooks(emu,vfs)
 state={'phase':'boot','instructions':0,'ntos':False,'key':False};events=[]
 def event(message):events.append(message);print(message,flush=True)
 def anchor(m):
  if state['phase']=='boot':
   state['phase']='hh3';state['at']=state['instructions']
   if args.direct_hh3:
    raw=(ROOT/'dist/NTOS.hh3').read_bytes();entry=struct.unpack_from('>I',raw,24)[0];phoff=struct.unpack_from('>I',raw,28)[0];phsize,phnum=struct.unpack_from('>HH',raw,42)
    for i in range(phnum):
     kind,offset,va,pa,filesize,memsize,flags,align=struct.unpack_from('>8I',raw,phoff+i*phsize)
     if kind==1:m.write_bulk(va,raw[offset:offset+filesize]+bytes(memsize-filesize))
    bindings=json.loads(Path(r'C:/Users/Phoebe/Downloads/hollyhock-3/versions/v02.01.7002.0001/addresses.json').read_text(encoding='utf-8'))['addresses']
    by_name={item['FunctionName']:int(item['startAddress'],16) for item in bindings}
    for source in Path(r'C:/Users/Phoebe/Downloads/hollyhock-3/sdk/os/functions').glob('*.s'):
     for name,address in re.findall(r'DEFINE_OS_FUNC\s+(\w+)\s+(0x[0-9a-fA-F]+)',source.read_text(encoding='utf-8')):by_name[name]=int(address,16)
    patched=[];missing=[]
    for symbol,addr in symbols.items():
     if not 0x8cff0000<=addr<0x8d000000:continue
     try:
      marker,name_pointer=struct.unpack('>II',m.read_bulk(addr,8))
      if marker>>16 not in [0xdead,0xbeef]:continue
      name=read_cstr(m,name_pointer)
      if name in by_name:m.write_bulk(addr,struct.pack('>I',by_name[name]));patched.append(name)
      else:missing.append((symbol,name))
     except Exception:pass
    event('SDK bindings '+str(len(patched))+' missing='+repr(missing))
    m.cpu.regs.r4=m.cpu.regs.r5=m.cpu.regs.r6=0;m.cpu.regs.pc=entry;m.cpu.regs.pr=0x80009adc;event('SDK ELF entry injected (HH3 picker bypass)')
   else:
    m.write_bulk(launch.MAGIC_ADDR,(0x5acb).to_bytes(2,'big'));m.cpu.regs.pc=launch.LOADER;m.cpu.regs.pr=0x80009adc;event('HH3 entered')
  return False
 elf=elf2ntx.Elf((ROOT/'dist/NTOS.elf').read_bytes());symbols={s.name:s.value for s in elf.symbols}
 def ntos(m):state['phase']='ntos';state['at']=state['instructions'];state['ntos']=True;event('NTOS main reached');return False
 def read_cstr(m,p):
  data=bytearray()
  for i in range(200):
   b=m.read_bulk(p+i,1)[0]
   if not b:break
   data.append(b)
  return data.decode('ascii','replace')
 def log(m):
  text=read_cstr(m,m.cpu.regs.r4)
  if text:
   event('NTOS: '+text)
   if text.startswith('Payload returned '):state['returned']=text
  return False
 emu.add_breakpoint(launch.ANCHOR,anchor);emu.add_breakpoint(symbols['_main'],ntos)
 for name,addr in symbols.items():
  if 'ntos_puts' in name:emu.add_breakpoint(addr,log)
 launch.boot(modules,emu,auto_launcher=False,vfs=vfs)
 def shot(index):
  def save(m):
   w,h,data=m.bus.disp_screenshot(modules.headless)
   Image.frombytes('RGBA',(w,h),struct.pack(f'<{len(data)}I',*data),'raw','BGRA').convert('RGB').save(out/f'{index}.png')
   event('SHOT '+index+' pc='+hex(m.cpu.regs.pc))
   if state['ntos']:
    state['sdk_size']={'width':320,'height':528}
    event('SDK SIZE '+repr(state['sdk_size']))
  emu.lock(save)
 last_phase='boot';start=time.time()
 for step in range(args.budget//1_000_000):
  emu.run(modules.disp,modules.touch,max_insts=1_000_000);state['instructions']+=1_000_000
  elapsed=state['instructions']-state.get('at',0)
  # Both picker menus contain exactly one executable entry in this isolated volume.
  pressed=state['phase'] in ('hh3','ntos') and 2_000_000<=elapsed<3_000_000
  clear=args.app=='control' and 22_000_000<=elapsed<23_000_000
  def resume(m):
   m.bus.disp_set_keys(modules.headless,kiudata_from_keys(['clear'] if clear else ['exe'] if pressed else []));m.cpu.regs.stop_cpu=False
  emu.lock(resume)
  if state['phase']!=last_phase:shot(state['phase']);last_phase=state['phase']
  if step%20==0:event('PROGRESS '+str(state['instructions'])+' phase='+state['phase']+' seconds='+str(round(time.time()-start)))
  if state['ntos'] and elapsed>=args.capture_after and not state.get('captured'):
   shot(args.app);state['captured']=True
  if state.get('returned'):
   shot('returned');break
  if args.app!='abi' and state.get('captured') and elapsed>=args.capture_after+5_000_000:break
 else:shot('timeout')
 (out/'result.json').write_text(json.dumps({'state':state,'events':events},indent=2))
 expected='Payload returned 0xAB1C0500' if args.app=='abi' else 'Payload returned 0x00000001'
 if args.app=='abi' and state.get('returned')!=expected:raise SystemExit('FAIL: missing expected return '+expected)
 if not state.get('captured') and not state.get('returned'):raise SystemExit('FAIL: no application capture')
if __name__=='__main__':main()



