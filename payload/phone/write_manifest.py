#!/usr/bin/env python3
"""Replace one fast app sidecar using an SVF authored with svf.py."""
import argparse,struct
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('exe',type=Path);ap.add_argument('name');ap.add_argument('svf',type=Path)
ap.add_argument('--id',type=int,required=True);ap.add_argument('--color',default='3960ff');ap.add_argument('--size',type=int,choices=[1,2],default=2);ap.add_argument('--live',action='store_true')
a=ap.parse_args();icon=a.svf.read_bytes();name=a.name.encode('utf-8')
if not a.exe.is_file():ap.error('EXE does not exist')
if not 1<=a.id<=65535 or not 1<=len(name)<=39:ap.error('ID must be 1..65535; name must fit 39 UTF-8 bytes')
if not icon.startswith((b'VEC1',b'VEC2')) or len(icon)>512:ap.error('SVF must be VEC1/VEC2, at most 512 bytes')
rgb=int(a.color.lstrip('#'),16)
if rgb>0xffffff:ap.error('color must be RRGGBB')
color=(rgb>>16)|(((rgb>>8)&255)<<8)|((rgb&255)<<16)
dest=a.exe.with_name(a.exe.name+'.manifest')
dest.write_bytes(b'PAM1'+struct.pack('>HBBIBBH',a.id,int(a.live),a.size,color,len(name),0,len(icon))+name+icon)
print(dest)
