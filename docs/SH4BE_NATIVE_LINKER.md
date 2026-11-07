# Experimental native GNU binutils SH4BE PE linker

This directory contains an **experimental, source-level patch to GNU
binutils**. It is deliberately separate from the production
`sh4a_nofpueb-elf` build and its known-working PE compatibility packer.

CI: `.github/workflows/native-sh4be-pe.yml`

Patch: `tools/patch_binutils_sh4be.py`

Smoke test: `tools/test_native_sh4be_pe.sh`

## Why distro installation alone is insufficient

The Hollyhock GCC toolchain only contains GNU `shelf`, `shlelf`, `sh`
and `shl` linker emulations. The existing upstream Windows CE
`shpe` BFD vector is **little-endian for both machine data and file
headers**. Even installing upstream `sh-unknown-pe` provides the
little-endian backend, not an executable that can run as ClassPad SH4BE.

GNU's ordinary big-endian `coff-sh` backend is not a PE image writer:
its header encoding is big-endian, and it lacks the full MZ/PE
executable layout. We need **big-endian machine data but little-endian
PE headers**.

## Patch design

The source patch extends the existing BFD SH PE backend, rather than
implementing another PE writer:

1. Derive a new `CREATE_SH4BE_PE_TARGET_VEC` from BFD's canonical COFF
   target vectors; byte-order for **data** is big, **header** little.
2. Build `pe-shbe` (COFF object) and `pei-shbe` (PE image) targets
   using `coff-sh.c`, `peXXigen.c`, and upstream GNU's Windows CE
   relocation support.
3. Register both vectors in `bfd/config.bfd`, `targets.c`, and
   `bfd/configure`.
4. Add an `ld` `shbepe` emulation derived from GNU's `shpe`.
5. Require native GNU `objcopy` to translate SH4BE ELF objects to
   `pe-shbe` objects and GNU `ld -m shbepe` to produce an MZ/PE
   executable directly. The Python PE packer is **not** used in that
   smoke test.

The new target is built from distro-provided GNU binutils source (2.44
on the CI container at the time this file was added), as a separate
build with `--target=sh-unknown-pe`.

## Machine type

Microsoft's PE specification assigns:

- `0x01A6`: Hitachi SH4 (little-endian in ordinary Windows CE)
- `0x01A8`: Hitachi SH5
- No registered SH4 big-endian PE machine number.

Therefore `0x01A7` is an **NTOS-private identifier**, not an
officially recognized SH4BE number. The loader recognizes both
`0x01A7` and legacy `0x01A8`, but the shipping Python packer still
writes `0x01A8` until the experimental native linker passes the
same end-to-end tests. Never silently reinterpret all existing SH5
images as standard SH4BE.

References:

- https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#machine-types
- https://gnu.googlesource.com/binutils-gdb/+/37f7f684de0e53c8add5ff43d73082bf98985321/include/coff/sh.h

## Production promotion gates

The native GNU linker **must** demonstrate all of:

- Genuine MZ + PE32 image, with machine `0x01A7`, CE subsystem,
  executable entry point, and linked SH4BE instructions.
- Correct native DLL exports from COREDLL and XAMLUI.
- Separate named PE imports for COREDLL.DLL and XAMLUI.DLL, including
  DLL-to-DLL imports, with writable big-endian IAT snapping.
- Correct `.reloc` entries and executable addresses at each of the
  preferred bases (0x00400000, 0x00800000 and 0x00C00000).
- Actual hardware execution: HELLOCORE, MEMTOOL, METROWIN, and GUIWIN.
- A reversible toolchain switch in `Makefile`; production packer
  remains as fallback until tests pass.

Passing the basic native-linker smoke test alone does **not**
establish that import library generation or runtime execution works.
