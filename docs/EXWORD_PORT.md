# NTOSH EX-word — flat _CONTENT SH4BE PE loader

The native Exword add-in is **ntos_sh.d01**, a gint-exword 528×320
D01. Exword transfer installs only files adjacent to the D01; it
**does not upload nested directories**, and requires DOS **8.3 names**
(8-character stem, dot, 3-character extension, max 12 total).

## Installed files

Both [Exword CI](../.github/workflows/exword-d01.yml) locales now ship one
flat set of PE modules beside the D01 in NTOSH/_CONTENT:

    NTOSH/_CONTENT/
      ntos_sh.d01
      HELLO.EXE
      HELL6720.EXE   (the compiled HELLOCORE.EXE)
      COREDLL.DLL
      XAMLUI.DLL
      VMTEST.EXE      (adapted from payload/vmtest.c for Exword)
      UIXM57EB.DLL   (the compiled UIXMOBILE.DLL)
      ...other SH4BE EXEs and DLLs, all 8.3 names

No SYSROOT or Windows/System32 folder is installed or searched. The file
picker boots in _CONTENT, with LEFT/RIGHT switching native disks, UP/DOWN
selecting across 16-entry pages, EXE launching, EXIT returning at root and
ACON or MENU quitting.

On native filesystems the launcher searches, in order of preference:

- \`\\drv0\NTOSH\_CONTENT\` or \`\\drv0\CASIO\NTOSH\_CONTENT\`
- \`\\crd0\CASIO\NTOSH\_CONTENT\` or \`\\crd0\NTOSH\_CONTENT\`

All DLL imports resolve in the **running EXE's own directory**, i.e.
the same flat _CONTENT directory. Names within WinCE PEs are unchanged;
for example \`UIXMOBILE.DLL\` maps to the deterministic 8.3 filename
\`UIXM57EB.DLL\` at lookup. The Python packager and native runtime use
the same uppercase FNV-1a suffix. CI rejects collisions and checks that
all deployed PE bytes match the shared HH3 build output.

## Build

With the separate Exword toolchain installed, \`make exword\` compiles the
ordinary NTOS \`ntx\`/phone binaries, compiles the special Exword VMTEST
from the same test source, builds the native D01 and copies all EXE/DLL/
CPL files into both locale install packages. CI uses two job containers:
the normal Hollyhock-3 SH4BE compiler for the common PEs, and
fxsdk-exword/gint-exword for the D01.

The payload files are *not embedded* in the D01's 128 KiB image.

## Advanced tests / VM

An existing CP400-oriented VMTEST reads NtosApi metadata from an unmapped
P0 address \`0x08100000\`, causing a **fatal Exword TLB miss**. The special
Exword VMTEST build instead receives the NtosApi function table via its
entrypoint. The native host implements indices 23, 38, 41–46 and 51–53
(reserve, commit, protect, query, release and committed-page count)
with a bounded **P1-backed physical page arena**. These APIs simulate
page permissions in software; read-only pages are *not* hardware-enforced.

The loader rejects recognizable CP400-only API-window references in
the executable before running it. It does not install CP400 P0 TLB entries,
take the firmware VBR, or promise that arbitrary WinCE guest faults are
recoverable. Unsupported guest MMU/protection use still requires a
proper Exword-owned MMU lease and exception handling.

The executable memory arena remains restricted to firmware profiles
with known safe experimental P1 addresses (XD-A8500 ROM32A and
XD-D6600 CY436 ROM32A). A generic host-owned executable allocator is
required before other ROMs can be supported safely. Always retest on
backed-up hardware; successful CI is not a device execution proof.

## XD-A8500 ROM32A MMU analysis

Source: uploaded ROM32A.BIN (33,554,432 bytes, SHA-256
`224df7b13ea27666f8ea5d24ab3a096784fddd0ab57961c61cce2b3e445cb7fc`) 
and its decompilation ROM32A.BIN.c. The three signature bytes in
`exword/profile.cpp` match this ROM exactly.

The ROM's `MMU_InitializeAddinAddressSpace` writes `MMUCR=0x104`
(disable and invalidate), installs translation handlers, resets the
add-in mappings, then enables with `MMUCR=0x101`. The firmware's
`MMU_InstallTLBEntry` sets `PTEH`/`PTEL`, ORs `0x110`, and executes
`LDTLB`. `MMU_HandleTranslationFault` dispatches:

- VAs below `0x70000000`: direct 8 MiB physical alias
  `0x10000000 | (VA & 0x007ff000)`, final PTEL `0x17c`.
  The CP400 `0x08100000` API window would map to **0x10100000**,
  not to NTOSH's NtosApi table.
- `0x70000000..0x73ffffff`: configured add-in primary mapping,
  final PTEL `0x15c` (**read-only**).
- `0x74000000..0x77ffffff`: configured add-in API mapping,
  final PTEL `0x17c` (**read/write**).
- Outside the configured ranges: firmware's non-returning
  `MMU_HaltOnTLBError` loop.

The ROM routes events `0x40/0x60/0x80` to the translation handler,
`0xa0/0xc0` to protection errors, and `0xe0/0x100` to address errors.
`OS_InstallExceptionHandler` modifies a shared dispatch table at
`0xfd800000` and returns the previous handler. This is evidence
of firmware-owned exception state, **not** a guest-private MMU lease.
`MMU_ConfigureAddinMappings` changes global window addresses and
flushes the TLB; invoking it while gint/NTOSH runs would invalidate
the active add-in mappings.

The read-only model and regression are `exword/rom32a_mmu.hpp` and
`tests/exword_rom_mmu_test.cpp`. `ROMDIAG.EXE` prints the real
MMUCR, VBR and SR without changing any hardware registers, then
waits for a keypress. It is the next useful diagnostic on an XD-A8500.

New physical-P1-safe test variants:

- `MMTEST.EXE`: 96-page resident P1 stress (not a UTLB eviction test).
- `MMROTEST.EXE`: PE-section rights query (metadata only, no true RO trap).
- `NATIVEVM.EXE`: NtAllocate/Protect/Free with checked writable guest pointers.
- `MMREUSE.EXE`, `MMSTRESS.EXE`: P1 decommit/recommit/zeroing and 500 cycles.

The CPU hardware does **not** enforce guest P1 page permissions.
Therefore these EX-word results are not equivalent to Hollyhock's
P0 TLB invalidation, protected store, or thread-fault tests.
`MMGUARD`, `KEXCEPT`, user-mode `KTHREAD`, arbitrary P0 faults
and actual multi-thread preemption remain disabled until a validated
Exword-owned exception/TLB scheme exists. The ROM source alone
cannot establish safe ownership of those resources.
