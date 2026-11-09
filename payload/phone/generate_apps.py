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
 elif name=='edge':v.circle(16,16,13).line(3,15,28,15).line(8,21,24,21)
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
 elif name=='settings':v.circle(16,16,10).circle(16,16,4).line(16,1,16,5).line(16,27,16,31).line(1,16,5,16).line(27,16,31,16)
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
def generate_assets(out):
 out=Path(out);root=out/'Apps';root.mkdir(parents=True,exist_ok=True);records=[]
 for app_id,(slug,name) in enumerate(APPS,1):
  folder=root/slug;folder.mkdir(exist_ok=True);svf=icon(slug)
  color={'excel':0x426f1c,'powerpoint':0x1f42c5,'note':0x782d81,'weather':0xb16400,'news':0x495b18}.get(slug,0xff6039)
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
 if not prefs.exists() or prefs.read_bytes()==old:
  prefs.write_bytes(b'PIN1'+struct.pack('>H',len(ids))+b'\0\0'+b''.join(struct.pack('>HBBBBH',i,1 if i in [18,10,11,24,20,6,15] else 2,x,y,0,0) for i,(x,y) in zip(ids,positions)))
 return records
if __name__=='__main__':
 generate_headers()
 print('Generated system_icons.h')
