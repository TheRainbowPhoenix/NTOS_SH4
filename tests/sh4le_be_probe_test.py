#!/usr/bin/env python3
"""PE endian probe regressions: reject blind conversion; preserve literal/data."""
import importlib.util
import struct
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('probe',ROOT/'tools/sh4le_be_probe.py')
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

# One small self-contained SH4LE PE with .text, intentionally mixed code
# and embedded literal words. Only first two instruction halfwords reviewed.
d=bytearray(0x400)
d[:2]=b'MZ'
struct.pack_into('<I',d,0x3c,0x40)
d[0x40:0x44]=b'PE\0\0'
struct.pack_into('<HH',d,0x44,0x1a6,1)
struct.pack_into('<H',d,0x54,224)
o=0x58
struct.pack_into('<H',d,o,0x10b)
struct.pack_into('<I',d,o+16,0x1000)
struct.pack_into('<I',d,o+28,0x10000)
struct.pack_into('<H',d,o+68,9)
struct.pack_into('<I',d,o+92,16)
sh=o+224
d[sh:sh+8]=b'.text\0\0\0'
struct.pack_into('<IIII',d,sh+8,16,0x1000,0x200,0x200)
struct.pack_into('<I',d,sh+36,0x60000020)
# First two SH4LE instruction halfwords, then literal with 0xF*** pattern
# which a linear pass would incorrectly classify as an executed FPU opcode.
d[0x200:0x208]=bytes.fromhex('09 00 0b 00 01 f0 45 67')
meta=mod.parse(d)
assert meta['machine']==0x1a6
r=mod.inspect(d)
assert r['fpu_family_candidates_count']>=1
assert not r['safe_for_automatic_conversion']
for ranges in ([],[[0x1000,0x1003]],[[0x1000,0x1004],[0x1002,0x1006]],
               [[0x1000,0x1202]],[[0x0,0x2]]):
    try:mod.convert_manual(d,ranges)
    except ValueError:pass
    else:raise AssertionError('accepted unsafe ranges '+repr(ranges))
result=mod.convert_manual(d,[[0x1000,0x1004]])
assert result[0x200:0x208]==bytes.fromhex('00 09 00 0b 01 f0 45 67')
assert result[:0x44]==d[:0x44]
assert result[0x48:0x200]==d[0x48:0x200]
assert result[0x204:]==d[0x204:]
assert mod.parse(result)['machine']==0x1a8
print('PASS conservative SH4LE PE inspection and reviewed-range swap, rejected dangerous ranges')
