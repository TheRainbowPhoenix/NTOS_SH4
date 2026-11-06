#!/usr/bin/env python3
"""Produce synthetic LE/experimental SH4BE PE32 import test images (not runnable)."""
import struct
import sys
from pathlib import Path
be = len(sys.argv)>2 and sys.argv[2]=='be'
fmt = '>' if be else '<'
def w16(b,p,v):struct.pack_into(fmt+'H',b,p,v)
def w32(b,p,v):struct.pack_into(fmt+'I',b,p,v)
raw=bytearray(0x600)
raw[:2]=b'MZ';struct.pack_into('<I',raw,0x3c,0x80)
raw[0x80:0x84]=b'PE\0\0'
coff=0x84;op=coff+20
w16(raw,coff,0x1a8 if be else 0x1a2)
w16(raw,coff+2,2)
w16(raw,coff+16,224)
w16(raw,coff+18,0x103)
w16(raw,op,0x10b)
w32(raw,op+16,0x1000)
w32(raw,op+28,0x10000)
w32(raw,op+32,0x1000);w32(raw,op+36,0x200)
w32(raw,op+56,0x3000);w32(raw,op+60,0x200)
w32(raw,op+92,16)
w32(raw,op+104,0x2000);w32(raw,op+108,0x40)
sh=op+224
for i,(name,rva,raw_off) in enumerate(((b'.text',0x1000,0x200),(b'.idata',0x2000,0x400))):
 p=sh+i*40;raw[p:p+8]=name.ljust(8,b'\0');w32(raw,p+8,0x200)
 w32(raw,p+12,rva);w32(raw,p+16,0x200);w32(raw,p+20,raw_off)
 w32(raw,p+36,0x60000020 if i==0 else 0xc0000040)
raw[0x200:0x202]=b'\x00\x09'
w32(raw,0x400,0x2040);w32(raw,0x40c,0x2080);w32(raw,0x410,0x2060)
w32(raw,0x440,0x20a0);w32(raw,0x444,0x8000002a)
w32(raw,0x460,0x20a0);w32(raw,0x464,0x8000002a)
raw[0x480:0x48c]=b'COREDLL.DLL\0'
raw[0x4a2:0x4a2+19]=b'OutputDebugStringA\0'
Path(sys.argv[1]).write_bytes(raw)
print('generated',sys.argv[1],'BE' if be else 'LE')
