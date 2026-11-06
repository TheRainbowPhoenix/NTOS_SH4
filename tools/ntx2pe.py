#!/usr/bin/env python3
"""Convert a relocatable raw SH4BE NTX into an NTOSABI1 MZ+PE32 demo.
With --elf, add a real COREDLL.DLL OutputDebugStringA import whose IAT
points to the ELF global imp_OutputDebugStringA. This is *not* WinCE CRT.
"""
import argparse, importlib.util, struct
from pathlib import Path

def aligned(n,a): return (n+a-1)//a*a
def convert(raw,elfpath=None):
    if struct.unpack_from('>H',raw,0)[0]!=0x01a8:
        raise ValueError('raw BE .NTX required')
    count=struct.unpack_from('>H',raw,2)[0]
    optlen=struct.unpack_from('>H',raw,16)[0]
    if count>7 or optlen<224:raise ValueError('unexpected raw .NTX')
    base=struct.unpack_from('>I',raw,20+28)[0]
    sections=[]
    for i in range(count):
        pos=20+optlen+i*40
        hdr=bytearray(raw[pos:pos+40])
        vsize,rva,rawlen,offset=struct.unpack_from('>IIII',hdr,8)
        sections.append([hdr,raw[offset:offset+rawlen],rva,vsize])
    opt=bytearray(raw[20:20+optlen])
    if elfpath:
        here=Path(__file__).resolve().parent
        spec=importlib.util.spec_from_file_location('elf2ntx',here/'elf2ntx.py')
        packer=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(packer)
        elf=packer.Elf(Path(elfpath).read_bytes())
        symbol=next((x for x in elf.symbols
                     if x.name=='imp_OutputDebugStringA'),None)
        if symbol is None:raise ValueError('missing imp_OutputDebugStringA')
        iat_rva=symbol.value-base
        if iat_rva>=struct.unpack_from('>I',opt,56)[0]:
            raise ValueError('IAT symbol outside image')
        rva=aligned(max(x[2]+x[3] for x in sections),0x1000)
        data=bytearray(96)
        struct.pack_into('>IIIII',data,0,rva+40,0,0,rva+48,iat_rva)
        struct.pack_into('>II',data,40,rva+64,0)
        data[48:60]=b'COREDLL.DLL\0'
        data[64:64+2+len(b'OutputDebugStringA\0')]=b'\0\0OutputDebugStringA\0'
        hdr=bytearray(40);hdr[:8]=b'.idata\0\0'
        struct.pack_into('>II',hdr,8,len(data),rva)
        struct.pack_into('>I',hdr,36,0xc0000040)
        sections.append([hdr,data,rva,len(data)])
        struct.pack_into('>II',opt,96+1*8,rva,40)
        struct.pack_into('>I',opt,56,aligned(rva+len(data),0x1000))
    header_end=0x84+20+len(opt)+40*len(sections)
    out=bytearray(aligned(header_end,0x200))
    out[:2]=b'MZ'
    struct.pack_into('<I',out,60,0x80)
    out[0x40:0x48]=b'NTOSABI1'
    out[0x80:0x84]=b'PE\0\0'
    out[0x84:0x98]=raw[:20]
    struct.pack_into('>H',out,0x84+2,len(sections))
    out[0x98:0x98+len(opt)]=opt
    off=0x98+len(opt)
    for hdr,data,_,_ in sections:
        target=aligned(len(out),0x200)
        out+=bytes(target-len(out))
        struct.pack_into('>II',hdr,16,len(data),target)
        out[off:off+40]=hdr
        off+=40
        out+=data
        out+=bytes(aligned(len(out),0x200)-len(out))
    struct.pack_into('>I',out,0x98+60,aligned(header_end,0x200))
    return out
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ntx',type=Path)
    parser.add_argument('-o','--output',type=Path,required=True)
    parser.add_argument('--elf',type=Path)
    args=parser.parse_args()
    data=convert(args.ntx.read_bytes(),args.elf)
    args.output.write_bytes(data)
    print('wrote',args.output,len(data),'bytes; NTOSABI1')
if __name__=='__main__':main()
