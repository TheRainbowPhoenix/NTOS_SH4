# NTOS SH4BE paging validation — emulator first

The default NTOS build is the known-working eager UTLB loader.
The experimental VBR+0x400 fault handler is **not** validated on real
ClassPad hardware. Do not replace your working HH3 install with the
experimental firmware until it survives emulator tests and host recovery.

## Build (Hollyhock container)

```sh
git pull origin main
make clean
make -j4
make check-pe
make pager-experiment
```

The default firmware stays at `dist/NTOS.hh3`.
The separately compiled VBR-experiment firmware is
`dist_pager/NTOS.hh3`. Its objects are under `obj_pager`; unlike
manually changing `DEFINES`, this avoids mixing regular/experimental
objects. Both modes use the standard PE files from `dist/`.

## Emulator test A: standard loader (no IRQ interception)

Install `dist/NTOS.hh3` and the required files under `\\fls0\\`.

- `CHAINTEST.EXE`, `CHAIN.DLL`, `COREDLL.DLL`: expected
  `Payload returned 0x373F373F` (already verified on user's emulator).
- `STRESSEXEC.EXE`, `ST01.DLL` through `ST12.DLL`, plus
  `COREDLL.DLL`: expected `Payload returned 0x1234004E`.
  This tests 12 DLLs at the shared preferred base `0x10000000`,
  including the four-way fanout of ST01.
- `PAGERTEST.EXE` and `PAGERSTRESS.DLL`: **expected clean rejection**
  with `PE too large for 55-page UTLB budget (needs pager)`.
  It should not hang or crash. **Press any key to return to the NTOS
  EXE list**, then CLEAR to exit the host.
- Test GUIWIN and METROWIN for regression, then EXE/CLEAR navigation
  returning to the host without screen corruption.

## Emulator test B: experimental RAM-backed UTLB miss handler

**2026-11-08 SH7305 correction:** the CP400 uses a 64-entry *fully
associative* UTLB, not a 32-set/4-way UTLB. The experimental pager now
manages 55 rotating entries by MMUCR.URC only; bits 5:4 are preserved.
Small PE graphs (55 or fewer total mapped pages) are fully primed before
execution, exactly as in normal NTOS. This should eliminate the
previous CHAINTEST/GUI fetch crash on unmapped code pages even in an
emulator that drops real TLB-miss exceptions. Large graphs remain
partially mapped: FAULTTEST is deliberately **not** changed to use hints.
These changes require fresh emulator verification.

**Emulator only until further validation.**

Install `dist_pager/NTOS.hh3` with all the standard PE files.
Do not mix an old COREDLL with newly rebuilt PE files.

1. Launch CHAINTEST. Expected `0x373F373F`, PF=0, HINT=0,
   BAD=0. This is now an eager-mapped regression and does **not**
   demonstrate interrupt delivery. If it still crashes, capture the
   UTLB slot contents and MMUCR/EXPEVT/TEA/SPC, not only PC.
2. Launch STRESSEXEC with all ST01..ST12 DLLs and COREDLL.
   Expected `0x1234004E`.
3. Launch PAGERTEST. The current PAGERSTRESS.DLL is **instrumented**
   with an NTOS-private page-hint gate. It explicitly asks the P1
   resident fault service to map each of the 80 virtual BSS pages
   before the SH4BE code touches it. This tests actual UTLB entry
   programming and eviction even if the emulator doesn't deliver VBR
   exceptions. Expected result `0xE0000CA8`.
4. The stage0 diagnostic now displays
   `PF=<hardware exceptions> HINT=<explicit hints> EV=<evictions> BAD=<failed faults>`.
   The instrumented test should show **HINT>=160, EV>0 and BAD=0**.
   PF=0 is acceptable if the emulator never forwards missing mappings
   as real TLB-miss exceptions. This would confirm a limitation in
   exception delivery, not demonstrate working hardware fault paging.
   The firmware must report nonzero PF on a true unhinted miss before
   its interrupt handler is considered functional.

   A `0xBAD10001` return means the first pass could not map a
   writable page; `0xBAD10002` means the second pass failed to
   restore a page before reading it.
5. **Only if PAGERTEST succeeds**, run `FAULTTEST.EXE` with
   `FAULTSTRESS.DLL` installed. This second DLL is the same 80-page
   workload compiled **without any page hints**, so it tests whether
   the emulator actually dispatches the SuperH VBR+0x400 exception.
   Expected checksum is also `0xE0000CA8`; expect PF>0, BAD=0.
   If it crashes but PAGERTEST passes, the remaining issue is the
   emulator's missing/incorrect hardware exception delivery or the
   SH4 VBR trampoline, rather than ordinary UTLB mapping.
6. Run PAGERTEST a second time. Then press CLEAR to quit NTOS and
   exercise the original Hollyhock UI and input. Host state restoration
   is an essential part of the test.

If the experimental image freezes, unexpectedly returns to the host,
or changes host input, **stop testing that image**. Restore the
default `dist/NTOS.hh3` build. Include the emulator CPU log, EXPEVT,
TEA, SPC, SSR, VBR, and the last PF/EV/BAD counters in a bug report.

## Important limits and next steps

This experimental pager maps already relocated physical RAM page frames.
It **does not** load PE pages from flash during a fault, and therefore
still requires enough RAM for each DLL's entire loaded image. The
separate `src/pe_pager.hpp` implements file-backed page materialization,
but connecting it to interrupt-safe storage I/O, dirty-page writeback,
and the actual HW miss path is not yet done.

The opt-in vector code compiles in CI, but compilation and host model
tests are not proof of a correct privileged exception return. No
claim of full WinCE NK parity or perfect hardware interrupt ownership
is made. Stock WinCE SH4LE PE files also remain incompatible with the
ClassPad's big-endian instruction execution.

## Diagnosing the user's original crash

The recorded emulator state has faulting `PC=0x01000208` and a
write to `0x01001400`, which belongs to the first BSS page of
PAGERSTRESS.DLL. However, `SPC=0x8CFF08DC` rather than the current
faulting PC, so the screenshot alone does **not** show that the CPU
actually entered our VBR+0x400 trap. Some emulator access guards can
report protected memory directly without delivering a SuperH exception.

The explicit hint mode lets us separate these possible failures:

- Hint mode succeeds, PF=0: UTLB slot installation works. This does
  **not** on its own prove emulator exception delivery is broken.
  A real CPU TLB miss must update TEA/EXPEVT/SPC/SSR and branch to
  VBR+0x400. The reported crash's SPC remains a host 0x8CFFxxxx
  address rather than the missing P0 instruction address.
- Hint mode fails at `0x01001400`: investigate UTLB permissions, cache
  synchronization and VA/PA mapping, not the VBR trampoline alone.
- PF>0 and BAD>0: the exception callback was reached but rejected
  the fault. Capture TEA, EXPEVT, SSR, SPC, and the last stage0 message.

This is not yet a disk-backed WinCE NK fault pager, and the VBR path is
experimental. Do not install the experimental HH3 on a physical CP400
before its emulator exceptions and rollback paths are verified.


## Current build validation and exact CP400 source cross-check

The uploaded `OS_02.01.7002.fls.c` shows `mmu_replace_entry_4k()`
writing `MMUCR = (MMUCR & 0xffff03ff) | (urc << 10)`, then PTEH/PTEL
and LDTLB; it does **not** write a UTLB-way value into bits 5:4.
The uploaded gint MMU driver identifies SH7305 UTLB address/data arrays
at P4 `0xF6000000`/`0xF7000000`, 64 entries (plus a separate
4-entry ITLB). This also matches the SH4A MMU architecture manual.

Rebuild on the latest `main`, rerun CHAINTEST and PAGERTEST in this
order, then FAULTTEST last. C/C++ CI success means the toolchain and
host-side test suite pass, **not** that the emulator has dispatched
a UTLB-miss exception. Expected counters for hint mode need not have
the exact old eviction count (105) after adopting fully associative
replacement; they must still show HINT>=160, EV>0, BAD=0 and correct
checksum. For FAULTTEST, the core discriminator is SPC: on a hardware
write miss at `0x01000208` to `0x01001400`, the CPU must capture
SPC=`0x01000208`, EXPEVT=`0x060`, TEA=`0x01001400` and jump to
VBR+0x400 before NTOS can handle it. If SPC remains at the old
host address and the emulator immediately aborts, exception injection
must be investigated in the emulator MMU backend.
