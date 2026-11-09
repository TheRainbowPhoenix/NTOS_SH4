#!/bin/sh
# Run from the repo root when the minimal HH3 image has no Python installed.
set -eu
export PATH=/toolchain/bin:$PATH
mkdir -p obj/phone
sh4a_nofpueb-elf-gcc -c payload/phone/tiny_memory.c -o obj/phone/tiny_memory.o -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns
for name in uixrender svf catalog fingerkb shellcontrols shellchrome uixn startmenu uixmobile starthost; do
 sh4a_nofpueb-elf-gcc -c payload/phone/$name.c -o obj/phone/$name.o \
  -std=c11 -Os -Wall -Wextra -Werror -Wno-misleading-indentation -ffreestanding -fno-builtin \
  -fno-stack-protector -fno-toplevel-reorder -fno-unwind-tables \
  -fno-asynchronous-unwind-tables -Ipayload/phone -Ipayload
 script=payload/xamlui.ld
 if [ "$name" = starthost ]; then script=payload/payload.ld; fi
 sh4a_nofpueb-elf-g++ -nostdlib -Wl,--emit-relocs -Wl,-e,_main \
  -T "$script" -o obj/phone/$name.elf obj/phone/$name.o obj/phone/tiny_memory.o
done
id=0
for slug in alarm calc camera support cortana excel files music maps message edge money video news note mail people phone photo powerpoint settings store weather word; do
 id=$((id+1))
 sh4a_nofpueb-elf-gcc -c payload/phone/stubapp.c -o obj/phone/app-$slug.o -std=c11 -Os -ffreestanding -fno-builtin -fno-toplevel-reorder -fno-stack-protector -DAPP_ID=$id -Ipayload/phone -Ipayload
 sh4a_nofpueb-elf-g++ -nostdlib -Wl,--emit-relocs -Wl,-e,_main -T payload/payload.ld -o obj/phone/app-$slug.elf obj/phone/app-$slug.o obj/phone/tiny_memory.o
done
