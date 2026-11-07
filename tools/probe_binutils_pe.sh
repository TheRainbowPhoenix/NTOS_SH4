#!/bin/sh
# Read-only probe: can the bundled GNU BFD toolchain natively emit SH4BE PE?
# This is diagnostic only, not the project's shipping converter.
set -u
echo "=== tool versions ==="
sh4a_nofpueb-elf-ld --version | head -2
sh4a_nofpueb-elf-objcopy --version | head -2
echo "=== linker emulations ==="
sh4a_nofpueb-elf-ld -V 2>&1 | head -50
echo "=== BFD file targets ==="
sh4a_nofpueb-elf-objcopy --info 2>&1 | grep -E 'pei-sh|pe-sh|coff-sh|elf32-sh|BFD header file' | head -35 || true
echo "=== SH4 BE ELF identity ==="
sh4a_nofpueb-elf-readelf -h obj/payload/hello.elf | grep -E '(Class|Data:|Machine:|Entry)' || true
echo "=== native ELF to PE backend experiment ==="
for target in pei-shl pe-shl coff-sh; do
    echo "target $target"
    sh4a_nofpueb-elf-objcopy -O "$target" obj/payload/hello.elf "/tmp/ntos-$target.bin" 2>&1 || echo "FAIL to output $target"
    if test -s "/tmp/ntos-$target.bin"; then
        sh4a_nofpueb-elf-objdump -f "/tmp/ntos-$target.bin" 2>&1 | head -12 || true
        od -An -tx1 -N 32 "/tmp/ntos-$target.bin" || true
    fi
done
echo "=== SH PE linker emulation attempt ==="
sh4a_nofpueb-elf-ld -m shlpe -o /tmp/ntos-ld-pe.exe -e _main obj/payload/hello.o 2>&1 || echo "FAIL shlpe emulation"
echo "=== probe finished ==="
