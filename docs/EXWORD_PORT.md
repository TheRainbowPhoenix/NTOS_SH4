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
entrypoint. The native host implements indices 23 and 41–46
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
