#!/bin/sh
# Run from the repo root when the minimal HH3 image has no Python installed.
set -eu
export PATH=/toolchain/bin:$PATH
mkdir -p obj/phone
for name in uixrender shellcontrols shellchrome uixn startmenu uixmobile starthost; do
 sh4a_nofpueb-elf-gcc -c payload/phone/$name.c -o obj/phone/$name.o \
  -std=c11 -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin \
  -fno-stack-protector -fno-toplevel-reorder -fno-unwind-tables \
  -fno-asynchronous-unwind-tables -Ipayload/phone -Ipayload
 script=payload/xamlui.ld
 if [ "$name" = starthost ]; then script=payload/payload.ld; fi
 sh4a_nofpueb-elf-g++ -nostdlib -Wl,--emit-relocs -Wl,-e,_main \
  -T "$script" -o obj/phone/$name.elf obj/phone/$name.o
done
