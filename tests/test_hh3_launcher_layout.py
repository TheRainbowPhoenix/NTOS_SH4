#!/usr/bin/env python3
"""Regression: reproduce exact launcher exit=1 caused by the Ke/Ps HH3.

The failing all-in-one NTOS.hh3 has:
    PT_LOAD[1] VA 0x8CFFE500 + MemSiz 0x1DFC = 0x8D0002FC.
The Hollyhock Custom Launcher throws before NTOS's entry point.
"""
import sys
import struct
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_hh3_launcher_layout import check

def fixture(data_bytes, *, datava=0x8CFFE500, datamem=0x1000):
    data = bytearray(512 + data_bytes)
    data[0:4] = b"\x7fELF"
    data[4] = 1
    data[5] = 2
    data[6] = 1
    struct.pack_into(">HHIIIIIHHHHHH",data,16,
                     2,42,1,0x8CFF0100,52,0,0,52,32,2,40,0,0)
    struct.pack_into(">IIIIIIII", data,52,
                     1,0x100,0x8CFF0000,0x8CFF0000,
                     0x200,0x400,5,0x80)
    struct.pack_into(">IIIIIIII", data,84,
                     1,0x200,datava,datava,
                     data_bytes,datamem,6,0x80)
    return bytes(data)

def rejects(data, margin=4096):
    try:check(data,margin)
    except ValueError:return
    raise AssertionError("did not reject invalid ELF segment")

def main():
    rejects(fixture(0x1500,datamem=0x1DFC))  # ACTUAL crash
    rejects(fixture(0x1000,datamem=0x1B00))  # still too little headroom
    n,space,end=check(fixture(0x1000,datamem=0x0A00),4096)
    assert n==2 and end==0x8CFFEF00 and space>=4096
    rejects(fixture(0x1000,datava=0x8CFD0000,datamem=0x1000))
    rejects(fixture(0x1000,datava=0x8CFCFF00,datamem=0x200))
    print("PASS launcher layout: reject original 0x8D0002FC overflow and reserved segment; accept 4KiB margin")

if __name__ == "__main__":
    main()
