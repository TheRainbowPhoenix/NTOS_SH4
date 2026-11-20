#!/usr/bin/env python3
"""Audit actual SH4BE probe ELF machine code, not only C source comments.

The controlled MD=0 window must contain exactly:
  [LDC SR] [NOP] [one MOV.B @r4,r0] [TRAPA #0x7B] [RTS] [NOP].
Absolutely no call, branch or stack operation may be inserted there.
TRAPA_PC+2 is the privileged-return address checked by NTOS.
"""
import struct
import sys
from pathlib import Path
from elf2ntx import Elf


def check(filename):
    elf = Elf(Path(filename).read_bytes())
    syms = {s.name: s for s in elf.symbols}
    begin = syms["_ntos_user_probe_stub"]
    trap = syms["_ntos_user_probe_trapa"]
    assert begin.shndx == trap.shndx, filename
    section = elf.sections[begin.shndx]
    off = begin.value - section.sh_addr
    assert off >= 0 and off+22 <= len(section.data), filename
    words = struct.unpack_from(">11H", section.data, off)
    # SH4 opcodes: STC SR,R0; MOV.L @(disp,PC),R1; AND R1,R0;
    # MOV.L @(disp,PC),R1; OR R1,R0; LDC R0,SR; NOP;
    # MOV.B @R4,R0; TRAPA #7B; RTS; NOP.
    assert words[0] == 0x0002, (filename, words)
    assert words[1] & 0xFF00 == 0xD100, (filename, words)
    assert words[2] == 0x2019, (filename, words)
    assert words[3] & 0xFF00 == 0xD100, (filename, words)
    assert words[4:11] == (0x201B, 0x400E, 0x0009,
                            0x6040, 0xC37B, 0x000B,
                            0x0009), (filename, words)
    assert trap.value == begin.value + 16, (filename, begin.value, trap.value)
    print(f"PASS SH4BE single-user-read opcode audit: {filename} "
          f"TRAPA=0x{trap.value:08X}, privileged RTS=0x{trap.value+2:08X}")


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        check(arg)
