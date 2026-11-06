# Windows CE PE32 / NK bootstrap POC

This is a **staging POC**, not yet a runnable WinCE kernel or SH3-to-SH4
binary translator. It deliberately does not overwrite SH4 MMU registers.

## Evidence from the supplied Windows CE binaries

The uploaded ctlpnl.exe, apcshell.exe, nk.exe, baseapp.dll and coredll.dll
are conventional MZ+PE32 files with machine **SH3 (0x01A2)**. The EXEs
have no PE base relocation directory because they are fixed-linked.
Their preferred bases are 0x00010000 (ctlpnl/apcshell), 0x80020000
(nk), 0x01d50000 (baseapp), and 0x01fb0000 (coredll).
A WinCE-compatible loader maps them at **those virtual addresses**.
Requiring .reloc unconditionally is not correct for WinCE.

## What this branch implements

- Standard MZ + PE32 (LE WinCE) and experimental custom **SH4BE** MZ+PE32
  recognition (custom machine 0x01A8, used by NTOS; not a standard WinCE
  SH4BE machine type).
- PE section mapping, BSS zero fill, HIGHLOW fixups where present, and
  safe fixed-base refusal when the target cannot be mapped at ImageBase.
- COREDLL.DLL import-table resolution by name or ordinal. The HH3 POC
  resolver only offers OutputDebugStringA, GetLastError, SetLastError and
  placeholder GetTickCount. Other imports fail explicitly.
- Calculator .EXE and .NTX discovery. Legacy raw BE .NTX executables still
  use their original loader.
- **Safe staging only** for MZ/PE files: NTOS parses, copies sections,
  inspects imports and computes the proposed VA->physical mapping. It
  **does not execute** fixed-image entry points yet.
- Linux host regression using synthetic little/big endian PE images with
  COREDLL IAT by-name and by-ordinal imports.

## World switch, MMU, WinCE NK

In the uploaded gint-test/src/kernel/world.c, gint snapshots CPU state and
per-driver state, unbinds before switching, and restores the original world
after the call. gint CPU state includes VBR, SR, CPUOPM, banked registers.
That is the pattern for restoring hardware safely, but does not create
WinCE user-space virtual mappings.

WinCE 5 source NK/KERNEL/SH/mdsh3.c initializes MMUTTB from SectionTable,
clears MMUPTEH/MMUTEA, enables translation via MMUCR and loads the
kernel page. **Do not copy those writes into an HH3 add-in**: the required
page tables, TLB refill handlers, privileged stack, interrupt vectors,
and return trampoline are not provided yet.

The next phase must allocate a page-aligned physical image; map
ImageBase/sections and WinCE shared KData into a *separate* TLB address
space; implement safe MMU state save/restore, exceptions and API dispatch;
then execute a real BE SH4 test EXE built against the stub COREDLL.

The original uploaded SH3 **LE machine code will not run on SH4 BE** simply
by changing PE fields. Proper conversion must account for SH3/SH4
instruction differences, endianness of constants, data, and calling ABI.

## Try it

Compile and test the host-side mapper:

```sh
python3 tests/test_ce_pe.py
# Optional: validate the uploaded WinCE images on your own machine
python3 tests/test_ce_pe.py ctlpnl.exe apcshell.exe nk.exe coredll.dll baseapp.dll
```

Build the HH3 as normal. Copy a PE as .EXE or .NTX to \\fls0\\.
The device prints PE machine/base and the staging result; it will not
jump into the WinCE entry until MMU/OS prerequisites are implemented.

The synthetic fixture generator `tools/make_ce_poc.py` checks format and
imports only: it does **not** contain a runnable CE application.
