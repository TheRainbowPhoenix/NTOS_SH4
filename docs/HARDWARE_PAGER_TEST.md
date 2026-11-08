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
  It should not hang or crash.
- Test GUIWIN and METROWIN for regression, then EXE/CLEAR navigation
  returning to the host without screen corruption.

## Emulator test B: experimental RAM-backed UTLB miss handler

**Emulator only until further validation.**

Install `dist_pager/NTOS.hh3` with all the standard PE files.
Do not mix an old COREDLL with newly rebuilt PE files.

1. Launch CHAINTEST. Expected `0x373F373F`.
2. Launch STRESSEXEC with all ST01..ST12 DLLs and COREDLL.
   Expected `0x1234004E`.
3. Launch PAGERTEST. PAGERSTRESS.DLL touches and rewrites 80 separate
   4 KiB virtual pages (larger than the 55-entry UTLB capacity) and
   verifies the values after eviction. Expected `0xE0000CA8`.
4. The stage0 diagnostic `PF=<count> EV=<count> BAD=<count>` should show
   a nonzero PF and EV on PAGERTEST, with BAD=0. Fault/eviction
   counts are dependent on the emulator and compiler and are not
   required to match one fixed number.
5. Run PAGERTEST a second time. Then press CLEAR to quit NTOS and
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
