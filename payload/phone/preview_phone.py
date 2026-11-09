#!/usr/bin/env python3
"""Compile and exercise the real C sources on a host, save exact RGB565 PNGs."""
import argparse,binascii,os,struct,subprocess,zlib
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
def png(ppm):
 data=ppm.read_bytes();header,raw=data.split(b'255\n',1)
 assert header==b'P6\n320 528\n' and len(raw)==320*528*3
 def chunk(kind,b):return struct.pack('>I',len(b))+kind+b+struct.pack('>I',binascii.crc32(kind+b)&0xffffffff)
 pixels=b''.join(b'\0'+raw[y*960:(y+1)*960] for y in range(528))
 ppm.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',320,528,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(pixels,9))+chunk(b'IEND',b''))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cc',default=os.environ.get('HOST_CC','gcc'));args=ap.parse_args()
 obj=ROOT/'obj/phone-host';out=ROOT/'dist/phone-preview';obj.mkdir(parents=True,exist_ok=True);out.mkdir(parents=True,exist_ok=True)
 flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-DPHONE_HOST','-I'+str(HERE),'-I'+str(ROOT/'payload')]
 rename={
 'uixrender':{'SpGetThreadContext':'render_SpGetThreadContext','SpRenderThreadInit':'render_SpRenderThreadInit','SpRenderThreadUninit':'render_SpRenderThreadUninit'},
 'shellcontrols':{'UIXGetSchemaFactory':'controls_UIXGetSchemaFactory','UIXSchemaUnloaded':'controls_Unloaded'},
 'shellchrome':{'UIXGetSchemaFactory':'chrome_UIXGetSchemaFactory','UIXSchemaUnloaded':'chrome_Unloaded'},
 'startmenu':{'UIXGetSchemaFactory':'menu_UIXGetSchemaFactory','UIXSchemaUnloaded':'menu_Unloaded','StartMenuInitialize':'menu_StartMenuInitialize','StartMenuUninitialize':'menu_StartMenuUninitialize'},
 'uixn':{n:'engine_'+n for n in ['UIXInitializeApplication','UIXLoadMarkup','UIXRunApplication','UIXShutdownApplication']},
 'uixmobile':{'RunApplication':'mobile_RunApplication'},'starthost':{'main':'phone_main'}}
 objects=[]
 for source,mapping in rename.items():
  o=obj/(source+'.o');objects.append(o)
  extra=['-Dphone_unsupported='+source+'_unsupported']+['-D'+a+'='+b for a,b in mapping.items()]
  subprocess.run([args.cc,*flags,*extra,'-c',str(HERE/(source+'.c')),'-o',str(o)],check=True)
 exe=obj/('phone-preview.exe' if os.name=='nt' else 'phone-preview')
 subprocess.run([args.cc,*flags,str(HERE/'host_preview.c'),*map(str,objects),'-o',str(exe)],check=True)
 subprocess.run([str(exe),str(out)],check=True)
 for p in out.glob('*.ppm'):png(p)
 print('Native C previews:',out)
if __name__=='__main__':main()
