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
    stem=Path(filename).stem.lower()
    write=stem=="mmguard"
    syscall=stem=="kthread"
    prefix=("_ntos_user_syscall_" if syscall else
            "_ntos_user_write_probe_" if write else
            "_ntos_user_probe_")
    begin = syms[prefix+"stub"]
    trap = syms[prefix+"trapa"]
    assert begin.shndx == trap.shndx, filename
    section = elf.sections[begin.shndx]
    off = begin.value - section.sh_addr
    count=10 if syscall else 12 if write else 11
    assert off >= 0 and off+2*count <= len(section.data), filename
    words = struct.unpack_from(">"+str(count)+"H", section.data, off)
    # SH4 opcodes: STC SR,R0; MOV.L @(disp,PC),R1; AND R1,R0;
    # MOV.L @(disp,PC),R1; OR R1,R0; LDC R0,SR; NOP;
    # MOV.B @R4,R0; TRAPA #7B; RTS; NOP.
    assert words[0] == 0x0002, (filename, words)
    assert words[1] & 0xFF00 == 0xD100, (filename, words)
    assert words[2] == 0x2019, (filename, words)
    assert words[3] & 0xFF00 == 0xD100, (filename, words)
    expected=(
        (0x201B,0x400E,0x0009,0xC37A,0x000B,0x0009) if syscall else
        (0x201B,0x400E,0x0009,0xE037,0x2400,
         0xC37B,0x000B,0x0009) if write else
        (0x201B,0x400E,0x0009,0x6040,0xC37B,0x000B,0x0009)
    )
    assert words[4:] == expected, (filename, words)
    assert trap.value == begin.value + (
        14 if syscall else 18 if write else 16
    ), (filename, begin.value, trap.value)
    kind="syscall" if syscall else "write" if write else "read"
    print(f"PASS SH4BE single-user-{kind} opcode audit: {filename} "
          f"TRAPA=0x{trap.value:08X}, privileged RTS=0x{trap.value+2:08X}")


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        check(arg)
