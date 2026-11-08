# NTOS PE32 module rebasing, WinCE references, and gint follow-up

## Why two DLLs may request the same ImageBase

PE32 ImageBase is a **preferred** address. A loader can allocate a different
virtual range when that address is occupied, provided the image has a valid
base-relocation directory. The loader must:

1. Reserve disjoint virtual ranges for EXE and every DLL.
2. Copy each image's sections to independent physical allocations.
3. Apply the module's base relocation fixups with unsigned
   `delta = assigned_address - PE_optional_header.ImageBase`.
4. Resolve imports against the **assigned virtual base of the exporting DLL**.
5. Flush caches and install the final VA→PA page mappings.
6. On exit, restore the host's MMU state and free the allocations.

### WinCE source findings (uploaded archives)

In the uploaded Windows Embedded CE 6 archive,
`PRIVATE/WINCEOS/COREOS/NK/KERNEL/loader.c` implements
`RelocatePage`. It computes `offset = BasePtr - eptr->e32_vbase`,
walks the FIX directory relocation blocks, and processes sites for
the destination page. It skips relocation when the actual base equals
the linked base. The surrounding loader supports page-in and copy
paths rather than depending on every DLL occupying the preferred VA.

CE 4.2 and CE 3.0 loaders contain related versions of that logic.

**Important architecture distinction:** On-device NTOS remains SH4BE
and now handles PE32 base fixups ABSOLUTE(0), HIGH(1), LOW(2),
HIGHLOW(3), and HIGHADJ(4), including WinCE's SH paired HIGH/LOW
adjustment. PE metadata is little-endian; each payload's data words
are big-endian on this machine. The same relocation engine can be
host-tested with little-endian data words. Other relocations are
rejected, never silently skipped.

The supplied sample WinCE binaries have PE machine `0x01A6`,
**SH4 little-endian**, so they CANNOT execute directly on the
SH4 big-endian ClassPad even after applying relocations.

### Actual uploaded WinCE binary audit

| File | Preferred ImageBase | Image size | PE HIGHLOW records | NTOS limitation |
| --- | --- | ---: | ---: | --- |
| TOOLHELP.DLL | 0x10000000 | 0x6000 | 31 | SH4LE code |
| DEMODISP.DLL | 0x10000000 | 0x5000 | 45 | SH4LE code |
| TCPSTK.DLL | 0x10000000 | 0x44000 | 4,951 | SH4LE + exceeds 55 pages |
| HWXCHS.DLL | 0x10000000 | 0x389000 | 1,253 | SH4LE + exceeds 55 pages |
| CERAMDRV.EXE | 0x00010000 | 0x4000 | none | fixed-base SH4LE |
| LOADDBG.EXE | 0x00010000 | 0x4000 | none | fixed-base SH4LE |

These are source-file-derived facts from the six uploaded PE binaries.
The read-only `tools/audit_wince_pe.py` can audit more specimens:

```sh
python3 tools/audit_wince_pe.py TOOLHELP.DLL TCPSTK.DLL HWXCHS.DLL
```

NTOS now links **both COREDLL and XAMLUI at the same preferred
`0x10000000` DLL address** to match WinCE conventions. The
SH7305 P0 first-fit allocator maps them to disjoint virtual ranges
(e.g. `0x01000000` and the next free aligned range), applies each
image's HIGHLOW relocations, then resolves the EXE and DLL imports
against their actual allocated virtual addresses. EXEs may use the
WinCE default `0x00010000` base if the P0 range is free.

The 55-slot UTLB limit remains: large 260 KiB and 3.7 MiB DLLs
require demand paging and an interrupt/TLB-miss service before they
can be executed even with native big-endian instructions.


## NTOS implementation

`src/pe_rebase.hpp` is freestanding, integer-only code for:

- `choose_base`: tries the requested base first; if occupied or it
  exceeds the four-way per-set UTLB budget, searches page-aligned
  alternatives beginning at `0x01000000`.
- `fits`: rejects overlap with reserved image ranges, invalid P0
  addresses, and excess occupancy in any of the 32 virtual-page sets.
  Accounts for the four pages of the API window at `0x08100000`.
- `apply`: validates every little-endian PE `.reloc` block and
  record **before writing**, then applies SH4BE HIGHLOW, standalone
  HIGH/LOW, paired HIGH+LOW, and HIGHADJ fixups. The independent
  `apply_with_endian` entry point also tests SH4LE record semantics
  offline, without claiming LE execution compatibility.

`src/main.cpp` chooses bases for the EXE, COREDLL, then XAMLUI;
applies each module's relocation table; snaps DLL dependencies in
dependency order; flushes the physical images; and maps the chosen
virtual addresses into the SH7305 UTLB.

This **does not** dynamically discover arbitrary third-party DLLs:
the current module loader still knows only COREDLL and XAMLUI.
It also keeps all images loaded for only one application execution.

## Collision testing

Normal build on HH3 v2.2.1:

```sh
make clean && make -j4
```

The independent CI C++ unit test `tests/pe_rebase_test.cpp` uses
the real COREDLL.DLL, XAMLUI.DLL, and METROWIN.EXE PE images to verify
preferred-base collision handling, address selection, import target
calculation, and malformed relocation rejection.

For an additional **real file fixture**, run:

```sh
make collision-test
```

This writes `dist_collision/XAMLUI.DLL`. It is an otherwise normal
PE XAMLUI.DLL with its preferred base and existing HIGHLOW fixups
retargeted to `0x01000000`, COREDLL's first actual P0 mapping.
Both shipping DLLs already share preferred `0x10000000`. This
file is **not used by the default build**. To try it on hardware,
install the normal NTOS.hh3, COREDLL.DLL, METROWIN.EXE and the
`dist_collision/XAMLUI.DLL` in place of the normal XAMLUI.DLL.
NTOS should print a rebased DLL address and launch METROWIN.

If the file has no relocation directory, the utility refuses to
produce a collision fixture. Never substitute a changed PE ImageBase
without changing the original absolute pointer words accordingly.

## gint-test: what is relevant, and what is deferred

The uploaded `gint-test(1).zip` contains:

- `src/kernel/world.c`: `gint_world_switch_in` and
  `gint_world_switch_out` unbind drivers, perform atomic CPU-state
  transitions, and preserve/reinitialize device state.
- `src/kernel/inth.S`: exception/interrupt vector gates and their
  carefully placed handler blocks.
- `src/kernel/exch.s`: exception callback/catcher plumbing, with
  explicit SR interrupt-mask handling.
- `src/kernel/hhk3_start.c`: resolves Hollyhock syscall symbols
  from HHK-provided environment tables.
- `src/kernel/tlbh.S`: has a CP-specific `NOMMU` branch, so its
  TLB miss handling cannot be dropped into our current CP400 UTLB
  mapped loader without adaptation.

Useful future direction: a separately gated NTOS kernel-mode world
switch that snapshots/restores VBR, SR, OS callbacks, all affected
interrupt masks, driver state, and the host MMU context before NTOS
owns interrupt handling. **Do not** copy the VBR replacement and
interrupt vector code straight into the existing loader: it currently
relies on Hollyhock's file, LCD and input SDK calls returning through
the host OS. Swapping interrupt worlds prematurely risks a hard crash
or leaving the host in a broken state.

`src/host_world.hpp` now adds a conservative gint-inspired **stage0
CPU context fence**: it captures the host SR, VBR, GBR and MMUCR
before mapping PE modules, rejects attempting mapped execution when
the host MMU already owns translation, and restores VBR/GBR/SR with
maskable interrupts masked before calling SDK memory services on exit.
It does **not** install a new ISR or repurpose gint driver handlers.

The uploaded `fxsdk-dev.zip` uses `-m4-nofpu -mb` for ClassPad,
and `gint-test/fxcp_hh3.ld.c` locates the initial HH3 image in
the host-provided P1 RAM region. NTOS retains that P1-resident stage0
model and its Hollyhock file/input/LCD bridge. gint's
`hhk3_start.c` resolves HHK symbol-table entries from `envp`;
NTOS instead uses its existing SDK/NTOS API bridge.

**Not yet implemented:** true driver-state save/restore, VBR interrupt
takeover, TLB-miss demand pager, large-image on-demand mapping,
arbitrary recursive DLL discovery, SH4LE instruction translation or
running unmodified stock Windows CE applications. Those require
separate hardware exercises; the current additions are compile- and
host-test verified, not verified on a physical ClassPad.
