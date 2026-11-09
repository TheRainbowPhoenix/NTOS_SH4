#!/usr/bin/env python3
"""Reject launcher ELFs whose API ends before the phone finder callbacks."""
import struct
import sys
from pathlib import Path
from elf2ntx import Elf

root=Path(__file__).resolve().parents[1]
paths=sys.argv[1:] or ['dist/NTOS.elf','dist_pager/NTOS.elf','dist_pager_verify/NTOS.elf']
for name in paths:
    path=root/name
    elf=Elf(path.read_bytes())
    def symbol(fragment):
        found=[s for s in elf.symbols if fragment in s.name and s.shndx<len(elf.sections)]
        assert len(found)==1,(path,fragment,len(found))
        return found[0]
    table=symbol('g_ntos_api')
    assert table.size>=27*4,(path,'old API: missing slots 24..26',table.size)
    section=elf.sections[table.shndx]
    offset=table.value-section.sh_addr
    for index,fragment in [(24,'ntos_find_first'),(25,'ntos_find_next'),(26,'ntos_find_close')]:
        target=symbol(fragment)
        actual=struct.unpack_from('>I',section.data,offset+index*4)[0]
        assert actual==target.value,(path,index,hex(actual),hex(target.value))
        assert elf.sections[target.shndx].sh_flags&4,(path,index,'callback is data, not code')
    assert b'Phone API 1 / 27 gates' in path.read_bytes()
    print('PASS',path.name,path.parent.name,'27 API gates; directory callbacks point to code')
