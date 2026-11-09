#!/usr/bin/env python3
"""Author binary manifests/SVF assets and stub resources. No gint required."""
from pathlib import Path
import struct,json
HERE=Path(__file__).resolve().parent
APPS=[('alarm','Alarms & Clock'),('calc','Calculator'),('camera','Camera'),('support','Contact Support'),
 ('cortana','Cortana'),('excel','Excel'),('files','File Explorer'),('music','Groove Music'),
 ('maps','Maps'),('message','Messaging'),('edge','Microsoft Edge'),('money','Money'),('video','Movies & TV'),
 ('news','News'),('note','OneNote'),('mail','Outlook Mail'),('people','People'),('phone','Phone'),
 ('photo','Photos'),('powerpoint','PowerPoint'),('settings','Settings'),('store','Store'),('weather','Weather'),('word','Word')]
class Vec:
 def __init__(self):self.b=bytearray(b'VEC1\x60'+struct.pack('>hh',32,32))
 def color(self,f=0xffff,s=0x7fff):self.b+=b'\x01'+struct.pack('>HH',f,s);return self
 def line(self,a,b,c,d):self.b+=b'\x30'+struct.pack('>hhhh',a,b,c,d);return self
 def rect(self,a,b,c,d):self.b+=b'\x10'+struct.pack('>hhhh',a,b,c,d);return self
 def circle(self,x,y,r):self.b+=b'\x20'+struct.pack('>hhh',x,y,r);return self
 def poly(self,*p):self.b+=bytes((0x40,len(p)//2))+struct.pack('>'+'h'*len(p),*p);return self
 def text(self,s):b=s.encode();self.b+=b'\x50'+struct.pack('>hhB',8,6,len(b))+b;return self
 def end(self):return bytes(self.b+b'\0')
def icon(name):
 v=Vec().color()
 if name=='phone':v.color(0x7fff).poly(5,2,11,2,15,10,10,14,18,22,22,17,30,22,28,29,24,30,15,25,6,16,2,8)
 elif name=='message':v.rect(2,4,29,23).line(14,23,14,30).line(14,30,24,23).line(7,12,10,12).line(14,12,17,12).line(21,12,24,12)
 elif name=='edge':v.color(0x7fff,0xffff).poly(30,18,8,18,9,23,13,26,21,27,28,24,25,29,17,31,9,29,3,24,1,17,4,8,10,3,18,1,25,3,30,8,31,14,27,14,25,9,20,7,14,7,10,10,8,14,30,14)
 elif name=='cortana':v.circle(16,16,14).circle(16,16,13)
 elif name in ['camera','photo']:v.rect(2,7,30,28);v.circle(16,17,8) if name=='camera' else v.poly(3,26,12,14,18,21,23,15,29,26)
 elif name=='weather':
  v.color(0x7fff).circle(16,16,7)
  for a,b,c,d in [(16,1,16,5),(16,27,16,31),(1,16,5,16),(27,16,31,16),(4,4,8,8),(24,24,28,28),(4,28,8,24),(24,8,28,4)]:v.line(a,b,c,d)
 elif name=='music':v.circle(16,16,14).color(0x7fff).circle(16,16,4).line(23,9,30,2)
 elif name=='video':v.color(0x7fff).rect(3,12,30,30).color().poly(2,5,28,1,30,7,4,11)
 elif name=='mail':v.rect(2,6,30,27).line(2,7,16,19).line(16,19,30,7)
 elif name in ['people','support']:v.color(0x7fff).circle(16,9,7).poly(4,31,5,23,10,19,22,19,27,23,28,31)
 elif name=='alarm':v.circle(16,18,11).line(16,18,16,10).line(16,18,22,20).line(3,5,10,2).line(23,2,30,5)
 elif name=='calc':
  v.rect(5,1,27,31).rect(8,4,24,11)
  for y in [15,21,27]:
   for x in [9,16,23]:v.line(x,y,x+1,y)
 elif name=='store':v.poly(4,9,28,6,28,30,4,27).circle(16,8,6).line(16,14,16,25).line(10,20,23,20)
 elif name=='settings':v.poly(13,1,19,1,20,6,24,8,28,7,31,12,27,16,28,20,31,23,28,28,23,27,19,29,18,32,12,32,11,27,7,25,3,26,0,21,4,17,3,13,0,10,3,5,8,6,12,4).circle(16,16,6)
 elif name in ['word','excel','powerpoint','note']:v.poly(2,3,22,1,22,31,2,28).rect(23,6,30,27).text({'word':'W','excel':'X','powerpoint':'P','note':'N'}[name])
 elif name=='maps':v.poly(1,5,11,1,21,6,31,1,31,27,21,31,11,26,1,31).line(11,1,11,26).line(21,6,21,31)
 elif name=='files':v.poly(2,7,12,7,15,11,30,11,30,29,2,29)
 elif name=='money':v.text('$')
 else:
  v.rect(2,3,30,29)
  for y in [8,14,20,25]:v.line(6,y,26,y)
 return v.end()
def system():
 result=[]
 for name in ['SHIFT','DELETE','SEARCH_BLACK','NEXT','PREV','CONNECT','BATTERY','FLASH','NOTE','VPN','LOCATION','AIRPLANE','CAMERA','CELL','HOTSPOT','MOON','SUN','WIFI','BLUETOOTH','LOCK','SETTINGS']:
  v=Vec().color(0xffff,0 if name=='SEARCH_BLACK' else 0x7fff)
  if name=='SHIFT':v.poly(4,15,16,3,28,15,22,15,22,29,10,29,10,15)
  elif name=='DELETE':v.poly(2,16,10,6,30,6,30,26,10,26).line(14,11,24,21).line(14,21,24,11)
  elif name=='SEARCH_BLACK':v.circle(13,13,10).line(21,21,30,30)
  elif name in ['NEXT','PREV']:v.circle(16,16,12).line(12 if name=='NEXT' else 20,9,20 if name=='NEXT' else 12,16).line(20 if name=='NEXT' else 12,16,12 if name=='NEXT' else 20,23)
  elif name=='CONNECT':v.rect(2,3,22,18).rect(17,12,30,30).line(10,21,14,21)
  elif name=='BATTERY':v.poly(16,1,8,10,10,22,16,29,22,22,24,10).line(16,1,16,31)
  elif name=='FLASH':v.rect(12,10,22,31).poly(8,1,26,1,22,10,12,10)
  elif name=='VPN':v.circle(9,9,6).line(14,14,29,29).line(23,23,28,18)
  elif name=='LOCATION':v.circle(16,5,3).poly(12,11,20,11,27,29,5,29,12,11)
  elif name=='AIRPLANE':v.poly(14,1,18,1,19,12,30,18,30,21,19,18,18,27,23,29,23,31,16,29,9,31,9,29,14,27,13,18,2,21,2,18,13,12)
  elif name=='CELL':
   for i in range(5):v.color(0x7fff).rect(2+i*6,28-i*5,6+i*6,31)
  elif name=='HOTSPOT':v.circle(16,12,10).circle(16,12,6).line(16,12,16,31)
  elif name=='MOON':v.poly(19,1,14,8,14,17,20,23,28,23,21,30,10,29,3,20,3,11,9,4)
  elif name=='WIFI':v.circle(16,22,14).circle(16,22,9).circle(16,22,4)
  elif name=='BLUETOOTH':v.poly(14,1,26,10,7,25,14,31,14,1,27,22,18,29).line(6,7,27,24)
  elif name=='LOCK':v.rect(7,14,25,30).circle(16,11,7).line(16,20,16,25)
  elif name in ['SUN','SETTINGS','CAMERA','NOTE']:result.append((name,icon({'SUN':'weather','SETTINGS':'settings','CAMERA':'camera','NOTE':'note'}[name])));continue
  else:v.rect(3,3,29,29)
  result.append((name,v.end()))
 return result
def generate_headers():
 items=system();lines=['/* Generated VEC1 icons, same decoder as application assets. */','typedef struct {const unsigned char *data;unsigned length;} SysIcon;']
 for i,(name,b) in enumerate(items):lines+=['#define SYS_'+name+' '+str(i),'static const unsigned char si'+str(i)+'[]={'+','.join(map(str,b))+'};']
 lines+=['static const SysIcon system_icons[]={'+','.join('{si%d,sizeof(si%d)}'%(i,i) for i in range(len(items)))+'};']
 (HERE/'system_icons.h').write_text('\n'.join(lines)+'\n')
 generate_settings_icons()
def generate_assets(out):
 out=Path(out);root=out/'Apps';root.mkdir(parents=True,exist_ok=True);records=[]
 for app_id,(slug,name) in enumerate(APPS,1):
  folder=root/slug;folder.mkdir(exist_ok=True);svf=icon(slug)
  # VEC1 app icons are mostly white legacy glyphs; soft light tiles use
  # slate/ink glyphs with subtle category color instead of harsh blue fields.
  color={'excel':0xd9efdd,'powerpoint':0xdbe5f7,'note':0xf3e1f4,
         'weather':0xd6edf9,'news':0xe3f0e1,'alarm':0xf3e9d5,
         'settings':0xe8e6f5,'calc':0xe7f1f9}.get(slug,0xf8f0e5)
  flags=1 if slug=='news' else 0
  b=b'PAM1'+struct.pack('>HBBIBBH',app_id,flags,2,color,len(name.encode()),0,len(svf))+name.encode()+svf
  (folder/(slug+'.exe.manifest')).write_bytes(b);(folder/(slug+'.svf')).write_bytes(svf)
  records.append({'id':app_id,'slug':slug,'name':name,'color':color,'flags':flags})
 # Install defaults only if no user preferences exist. Regenerating apps
 # must never overwrite pin edits made on a mounted device/package.
 prefs=out/'home.dat'
 ids=[18,10,11,1,16,17,5,19,22,8,13,23,14,12,2,9,24,20,6,15]
 old=b'PIN1'+struct.pack('>H',len(ids))+b'\0\0'+b''.join(struct.pack('>HBBBBH',i,1 if i in [18,10,11,24,20,6,15] else 2,0,0,0,0) for i in ids)
 positions=[(0,0),(0,1),(1,1),(2,0),(4,0),(0,2),(2,2),(4,2),(0,4),(2,4),(4,4),(0,6),(2,6),(4,6),(0,8),(2,8),(4,8),(5,8),(4,9),(5,9)]
 previous=b'PIN1'+struct.pack('>H',len(ids))+b'\0\0'+b''.join(struct.pack('>HBBBBH',i,1 if i in [18,10,11,24,20,6,15] else 2,x,y,0,0) for i,(x,y) in zip(ids,positions))
 layout=[(18,1,0,0),(10,1,0,1),(11,1,1,1),(1,2,2,0),(16,2,4,0),
  (17,2,0,2),(5,1,2,2),(22,1,3,2),(8,1,2,3),(13,1,3,3),(19,2,4,2),
  (23,2,0,4),(14,2,2,4),(12,2,4,4),(2,1,0,6),(9,1,1,6),(24,1,0,7),(20,1,1,7),(21,2,2,6),(6,1,4,6),(15,1,5,6)]
 defaults=b'PIN1'+struct.pack('>H',len(layout))+b'\0\0'+b''.join(struct.pack('>HBBBBH',i,size,x,y,0,0) for i,size,x,y in layout)
 if prefs.exists() and prefs.read_bytes()!=defaults:(out/'home-backup.dat').write_bytes(prefs.read_bytes())
 prefs.write_bytes(defaults)
 (out/'home-default.dat').write_bytes(defaults)
 cache=bytearray(b'CAT1'+struct.pack('>HBB',len(records),1,0))
 for a in sorted(records,key=lambda a:a['name'].lower()):
  name=a['name'].encode();path=('\\fls0\\Apps\\'+a['slug']+'\\'+a['slug']+'.exe').encode();svf=icon(a['slug'])
  cache+=struct.pack('>HBBIBBH IIB3x',a['id'],a['flags'],2,a['color'],len(name),len(path),len(svf),0,0,0)+name+path+svf
 (out/'apps.dat').write_bytes(cache)
 return records
def generate_settings_icons():
 items=[]
 for name in ['system','devices','network','personal','accounts','time','access','privacy','settings','display','notifications','phone','message','storage','battery','maps']:
  v=Vec().color(0xffff,0x1d9f if len(items)<9 else 0x7fff)
  if name in ['system','display']:v.rect(7,1,25,31).line(15,27,17,27)
  elif name=='devices':
   v.rect(1,2,27,16).rect(17,13,31,31).circle(24,24,3)
   for y in [6,10]:
    for x in [5,10,15,20]:v.line(x,y,x+1,y)
  elif name=='network':v.circle(16,16,15).poly(16,1,9,6,7,16,9,26,16,31,23,26,25,16,23,6).line(1,12,31,12).line(1,21,31,21)
  elif name=='personal':v.rect(1,9,27,26).line(11,30,20,30).poly(8,21,11,14,26,0,31,5,16,20,8,21)
  elif name=='accounts':v.circle(16,9,8).poly(2,31,5,22,11,18,21,18,27,22,30,31)
  elif name=='time':v.circle(12,12,11).line(12,4,12,12).line(12,12,18,12).text('A')
  elif name=='access':v.circle(16,4,3).line(2,11,30,11).line(16,8,16,23).line(16,23,7,31).line(16,23,25,31)
  elif name=='privacy':v.rect(5,12,27,31).circle(16,10,8).line(16,19,16,26)
  elif name in ['settings','phone','message','maps']:
   b=icon(name);b=b.replace(b'\x01\xff\xff\x7f\xff',b'\x01\xff\xff\x1d\x9f') if name=='settings' else b
   items.append((name,b));continue
  elif name=='notifications':v.poly(1,2,31,2,31,25,18,25,12,31,12,25,1,25)
  elif name=='storage':v.poly(6,1,27,1,27,31,6,31,6,1,10,6,22,6,22,1).line(10,10,10,17)
  elif name=='battery':v.rect(1,8,28,25).rect(28,13,31,20)
  items.append((name,v.end()))
 lines=['/* Generated settings SVFs: accent categories and white system rows. */','typedef struct {const unsigned char *data;unsigned length;} SettingsIcon;']
 assets=HERE/'assets/settings';assets.mkdir(parents=True,exist_ok=True)
 for i,(name,b) in enumerate(items):
  (assets/(name+'.svf')).write_bytes(b);lines.append('static const unsigned char sc%d[]={%s};'%(i,','.join(map(str,b))))
 lines.append('static const SettingsIcon settings_icons[]={'+','.join('{sc%d,sizeof(sc%d)}'%(i,i) for i in range(len(items)))+'};')
 (HERE/'settings_icons.h').write_text('\n'.join(lines)+'\n')
if __name__=='__main__':
 generate_headers()
 print('Generated system_icons.h')
