#!/usr/bin/env python3
"""Verify a real >4 MiB SH4BE PE, not a sparse/BSS-only image."""
import pathlib
import struct
import sys

def u16(data, off):
    return struct.unpack_from("<H", data, off)[0]
def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]

def main(path):
    data=pathlib.Path(path).read_bytes()
    assert len(data)>=4*1024*1024, "PE file must be >=4 MiB on disk"
    assert data[:2]==b"MZ"
    pe=u32(data,0x3c)
    assert data[pe:pe+4]==b"PE\0\0"
    assert u16(data,pe+4) in (0x01A7,0x01A8)
    sections=u16(data,pe+6)
    opt=pe+24
    assert u16(data,opt)==0x10b
    size_image=u32(data,opt+56)
    assert size_image>4*1024*1024, "Image must have >4 MiB virtual span"
    section_base=opt+u16(data,pe+20)
    largest=0
    for i in range(sections):
        sh=section_base+40*i
        raw=u32(data,sh+16)
        off=u32(data,sh+20)
        if raw>largest:
            largest=raw
        if raw>=4194304:
            assert off<=len(data) and raw<=len(data)-off
            for delta in (0,4095,4096,1048576,2097152,3145728,4194303):
                assert data[off+delta]==0xa5, (i,delta,hex(data[off+delta]))
    assert largest>=4194304, "No 4 MiB initialized PE section found"
    print(f"PASS {path}: disk={len(data)} image={size_image} initialized={largest}")

if __name__=="__main__":
    main(sys.argv[1])
