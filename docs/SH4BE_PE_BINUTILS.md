# SH4BE PE/COFF toolchain status and migration plan

## What GNU binutils 2.45 actually supports in the HH3 container

Verified using the official `ghcr.io/classpaddev/hollyhock-3:v2.2.2`
GitHub Actions build, with `tools/probe_binutils_pe.sh`:

| Operation | Result |
| --- | --- |
| Compile/link SH4A no-FPU big-endian ELF | Supported: `shelf` |
| GNU BFD output `coff-sh` (big-endian COFF) | Supported |
| GNU BFD output `pei-shl` (little-endian PE image) | Not compiled into this toolchain |
| GNU BFD output `pe-shl` | Not compiled into this toolchain |
| GNU ld PE emulation | Not present (`ld -V` reports only `shelf shlelf sh shl`) |

Upstream binutils has a **little-endian** Windows CE SH PE backend,
`bfd/pei-sh.c` / `pe-sh.c`, and an `ld` `shpe` emulation. Its
`include/coff/sh.h` also defines big-endian non-PE SH COFF
(`SH_ARCH_MAGIC_BIG=0x0500`), but these two capabilities **do not**
combine to produce a mixed-endian SH4BE PE image.

Sources:
- https://gnu.googlesource.com/binutils-gdb/+/refs/tags/binutils-2_44/ld/emulparams/shpe.sh
- https://sources.debian.org/src/binutils/2.45.50.20251201-1/bfd/pei-sh.c/
- https://gnu.googlesource.com/binutils-gdb/+/37f7f684de0e53c8add5ff43d73082bf98985321/include/coff/sh.h

**Important:** The existing NTOS private machine field `0x01A8`
identifies SH5 in the official Microsoft COFF tables, not an assigned
SH4 big-endian PE machine. `0x01A6` denotes little-endian SH4. The
private `0x01A8` convention is maintained for **current NTOS binary
compatibility**, but should not be confused with a standard WinCE
SH4BE target.

Reference:
https://learn.microsoft.com/en-us/windows/win32/sysinfo/image-file-machine-constants

## Why a blind objcopy substitution would break the loader

A correct NTOS SH4BE PE has:

1. SH4BE instructions, literal pools, pointers and writable IAT values.
2. Little-endian MZ/PE/COFF metadata, optional headers, import/export
   directories, name pointers, and HIGHLOW relocation blocks.
3. One import descriptor per named DLL, with proper OFT and FirstThunk.
4. A fully linked entry point and correct addresses in separate
   EXE, COREDLL and XAMLUI images.

`objcopy -O coff-sh` produces **big-endian COFF**, not an MZ/PE
executable. `objcopy -O pei-shl` fails with **invalid bfd target** in
the installed cross toolchain. Switching `Makefile` to either one would
break the PE loader or change the CPU byte order.

## Current verified build path

GNU `sh4a_nofpueb-elf-gcc` and `ld` remain authoritative for
compilation, symbol resolution, ABI, machine code, and placement.
`tools/elf2ntx.py` is currently retained **only as the PE container
writer** because upstream BFD in the bundled cross toolchain cannot
emit these mixed-endian images. It is not a native binutils PE linker.

Run:

```sh
make clean && make -j4
make check-pe
```

`check-pe` calls `tools/test_binutils_parity.sh` to round-trip
linked SH4BE payload code through native GNU BFD `coff-sh` and
check it byte-by-byte against the output PE's executable sections,
allowing only file-alignment zero padding. This comparison uses a
**temporary stripped copy** because the linker-created ELF files
retain debug/link-time relocation records from `--emit-relocs`
that cannot be mapped directly onto native COFF symbols.

The independent `tools/verify_pe_linkage.py` additionally:

- Opens the **finished PE** files, not the linker inputs.
- Validates executable entry points and section bounds.
- Reads COREDLL and XAMLUI export directories independently.
- Resolves each EXE and DLL import against the **correct named DLL**.
- Checks the symbol resolves into executable DLL code, never a
  forwarded export or a missing module.
- Confirms every IAT slot is in writable memory and starts at zero.
- Simulates NTOS's **big-endian** 32-bit IAT writes.
- Confirms that the PE `.text` bytes exactly match the GNU
  binutils-linked instructions.

The existing PE parser tests, UTLB set-occupancy test, and bitmap GDI
preview tests also run under CI. None can prove native execution on a
physical ClassPad; a hardware run is still required.

## Requirements before switching to a genuinely native GNU PE linker

1. Build a binutils target supporting SH4 **big-endian code/data**
   with **little-endian PE/COFF metadata**, rather than `pe-shl`.
2. Define a non-conflicting, documented machine field and update the
   loader to recognize it alongside existing NTOS files.
3. Extend BFD's SH COFF relocation backend to emit PE32 `.reloc`
   blocks for the required absolute 32-bit SH4 relocations.
4. Build link-time import libraries for COREDLL and XAMLUI (with
   module-specific `__imp_` entries and a null-terminated IAT).
5. Configure an `ld` PE emulation capable of linking SH4BE input
   objects to separate EXE/DLL preferred bases and generating real
   export/import directories.
6. Run identical link-time, byte-parity, import/export, and physical
   hardware tests on all existing demo payloads, then remove the
   compatibility packer from the default build.

**Do not remove the working PE packer before that native linker passes
the same tests.** File extensions or machine-header patches alone are
not sufficient.
