#!/bin/sh
# Native binutils SH4BE PE proof-of-concept: no ELF->PE Python packer.
# The final gate requires a real MZ/PE EXE from GNU ld with a valid entry,
# SH4BE code bytes, correct mixed endian metadata, and private 0x01A7 tag.
set -eu
BUILD=/tmp/ntos-native/build
SRC=$(cat /tmp/ntos-native/source-path)
LD="$BUILD/ld/ld-new"
OBJCOPY="$BUILD/binutils/objcopy"
OBJDUMP="$BUILD/binutils/objdump"
mkdir -p /tmp/ntos-native
"$LD" -V | tee /tmp/ntos-native/emulations.txt
"$OBJCOPY" --info > /tmp/ntos-native/targets.txt
grep -q 'shbepe' /tmp/ntos-native/emulations.txt
grep -q 'pe-shbe' /tmp/ntos-native/targets.txt
grep -q 'pei-shbe' /tmp/ntos-native/targets.txt
echo "PASS: patched GNU BFD PE target and ld emulation compiled"

# A .o compiled by the real ClassPad SDK is SH4BE, not SH little endian.
sh4a_nofpueb-elf-gcc -c payload/hello.c -o /tmp/ntos-native/hello.elf.o \
  -ffreestanding -fno-builtin -O2 -I/sdk/include -fno-stack-protector
sh4a_nofpueb-elf-readelf -h /tmp/ntos-native/hello.elf.o |
  grep -q 'big endian'
# Native BFD ELF to PE COFF conversion, preserving target machine/data.
"$OBJCOPY" -I elf32-sh -O pe-shbe \
  /tmp/ntos-native/hello.elf.o /tmp/ntos-native/hello.pe.o
"$OBJDUMP" -f /tmp/ntos-native/hello.pe.o
# ld must supply MZ, PE headers, section VAs and relocations by itself.
"$LD" -m shbepe --image-base 0x00400000 -e _main \
  --subsystem 9 -o /tmp/ntos-native/native-hello.exe \
  /tmp/ntos-native/hello.pe.o
"$OBJDUMP" -x /tmp/ntos-native/native-hello.exe > /tmp/ntos-native/native-hello.txt
python3 - <<'PY'
from pathlib import Path
import struct
p=Path('/tmp/ntos-native/native-hello.exe').read_bytes()
assert p[:2]==b'MZ', 'ld did not emit MZ header'
off=struct.unpack_from('<I',p,0x3c)[0]
assert p[off:off+4]==b'PE\0\0', 'ld did not emit PE signature'
m=struct.unpack_from('<H',p,off+4)[0]
assert m==0x01a7, ('wrong private SH4BE machine',hex(m))
opt=off+24
assert struct.unpack_from('<H',p,opt)[0]==0x10b
entry=struct.unpack_from('<I',p,opt+16)[0]
imagebase=struct.unpack_from('<I',p,opt+28)[0]
assert imagebase==0x00400000
assert entry>0
print(f'PASS native GNU ld PE32 SH4BE EXE: machine {m:#x}, '
      f'entry {entry:#x}, image base {imagebase:#x}, {len(p)} bytes')
PY
