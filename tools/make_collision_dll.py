#!/usr/bin/env python3
"""Create an OPTIONAL same-base DLL collision fixture for NTOS hardware tests.

NOT an ELF->PE packer. Takes a built PE DLL, applies its existing PE32
HIGHLOW base-relocation table to big-endian SH4 pointer words, and changes
the preferred ImageBase in the LE PE optional header. The import/export
directory RVAs and other PE metadata remain unchanged.

Example:
  python3 tools/make_collision_dll.py dist/XAMLUI.DLL \
      -o dist_collision/XAMLUI.DLL --base 0x00800000
Deploy this test XAMLUI.DLL instead of the production one; NTOS must
relocate it to another VA because COREDLL.DLL already owns 0x00800000.
"""
import argparse
from pathlib import Path
import struct


def u16(data, p):
    return struct.unpack_from("<H", data, p)[0]


def u32(data, p):
    return struct.unpack_from("<I", data, p)[0]


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("image",type=Path)
    ap.add_argument("-o","--output",required=True,type=Path)
    ap.add_argument("--base",type=lambda v:int(v,0),default=0x00800000)
    args=ap.parse_args()
    if args.base<0x00100000 or args.base&4095:
        ap.error("--base must be a page-aligned P0 virtual address")
    data=bytearray(args.image.read_bytes())
    assert data[:2]==b"MZ"
    pe=u32(data,0x3c)
    assert data[pe:pe+4]==b"PE\0\0"
    count=u16(data,pe+6)
    optional=pe+24
    assert u16(data,optional)==0x10b
    assert u16(data,pe+22)&0x2000, "input must be PE DLL"
    old=u32(data,optional+28)
    reloc_rva,reloc_size=struct.unpack_from("<II",data,optional+136)
    if not reloc_rva or reloc_size<8:
        raise ValueError("the DLL has no PE .reloc directory")
    sec=optional+u16(data,pe+20)
    secs=[]
    for i in range(count):
        p=sec+i*40
        va=u32(data,p+12)
        virt=u32(data,p+8)
        raw=u32(data,p+20)
        rawsize=u32(data,p+16)
        secs.append((va,max(virt,rawsize),raw,rawsize))
    headers=u32(data,optional+60)
    def offset(rva,needed):
        for va,span,raw,rawsize in secs:
            if rva>=va and rva-va<=rawsize and needed<=rawsize-(rva-va):
                return raw+(rva-va)
        if rva<=headers and needed<=headers-rva:return rva
        raise ValueError(f"non-file-backed relocation RVA {rva:#x}")
    delta=(args.base-old)&0xffffffff
    changed=0
    off=reloc_rva
    end=reloc_rva+reloc_size
    edits=[]
    while off<end:
        pos=offset(off,8)
        page,blocksize=struct.unpack_from("<II",data,pos)
        if blocksize<8 or blocksize%2 or blocksize>end-off:
            raise ValueError("invalid PE relocation block")
        for i in range(8,blocksize,2):
            entry=u16(data,offset(off+i,2))
            typ,within=entry>>12,entry&0xfff
            if typ==0:continue
            if typ!=3:raise ValueError(f"unsupported relocation type {typ}")
            target=offset(page+within,4)
            oldval=struct.unpack_from(">I",data,target)[0]
            edits.append((target,(oldval+delta)&0xffffffff))
            changed+=1
        off+=blocksize
    for target,newval in edits:
        struct.pack_into(">I",data,target,newval)
    struct.pack_into("<I",data,optional+28,args.base)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(data)
    print(f"collision DLL: {args.image} base {old:#010x} -> "
          f"{args.base:#010x}, {changed} BE HIGHLOW fixes; "
          f"saved {args.output}")


if __name__=="__main__":
    main()
