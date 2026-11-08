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

**Important architecture distinction:** WinCE's SH architecture also
supports paired HIGH/LOW relocation kinds and other CPU-specific
relocation records. Our SH4BE PE compatibility format currently emits
`IMAGE_REL_BASED_HIGHLOW` (3) for each absolute **big-endian** 32-bit
pointer, and `IMAGE_REL_BASED_ABSOLUTE` (0) for padding. This is not
a claim of full compatibility with every stock Windows CE SH DLL.
`ntos_rebase::apply` intentionally rejects other relocation kinds.

## NTOS implementation

`src/pe_rebase.hpp` is freestanding, integer-only code for:

- `choose_base`: tries the requested base first; if occupied or it
  exceeds the four-way per-set UTLB budget, searches page-aligned
  alternatives beginning at `0x01000000`.
- `fits`: rejects overlap with reserved image ranges, invalid P0
  addresses, and excess occupancy in any of the 32 virtual-page sets.
  Accounts for the four pages of the API window at `0x08100000`.
- `apply`: validates every little-endian PE `.reloc` block and
  record **before writing**, then applies 32-bit big-endian HIGHLOW
  adjustments. Fails closed for malformed, missing, truncated or
  unsupported fixups when rebasing is necessary.

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
retargeted to `0x00800000`, the SAME base as COREDLL.DLL. This
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

The present change is therefore limited to **PE DLL rebasing and
build reliability**; there is no new interrupt takeover code.
