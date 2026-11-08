# SH4AL-DSP CP400 NTOS pager — agent handoff (2026-11-08)

## Target and invariants
- Work on `main`; target fx-CP400 OS 02.01.7002, SH4AL-DSP **no FPU**, big-endian SH4 PE.
- NTOS.hh3 runs from the fixed-in-memory ELF/Hollyhock YAL stage0. Do not assume a conventional WinCE boot/kernel address space.
- Run host SDK calls only with the host VBR restored; exception callback must use P1-resident RAM and no heap, flash, LCD or SDK calls.
- Preserve default `dist/NTOS.hh3`; VBR experiments only in `dist_pager/NTOS.hh3`, emulator first.

## Reported emulator results at original fdc3e93
1. Normal NTOS rejects PAGERTEST's >55-page image as expected, but the error acknowledgment did not return to the picker.
2. PAGERTEST/PAGERSTRESS in experimental image: result `0xE0000CA8`, HINT=160, PF=0, EV=105, BAD=0. **Verified mapping+replacement via explicit hint service, not exception paging.**
3. FAULTTEST/FAULTSTRESS (no hints): emulator abort at PC `0x01000208` on `MOV.B r3,@(r0,r6)`, address `0x01001400`. CPU dump VBR=`0x8c311000`, SPC=`0x8cff0462`, SSR=`0x40001101`, SR=`0x40001100`. The saved SPC is NOT the faulting PC. The repeated stack trace `0x80021ede -> 0x80023296` is not evidence of NTOS vector entry. No fresh PF number was obtained because emulator aborted.

## Commits during this session
- `e11dbf2`: eliminate Debug_WaitKey / getKey mixing in loader-error acknowledgment; await release, a NEW key press, then its release. **Needs emulator check** (especially if getKey semantics differ on CP400).
- `e3b5bab` + `3502f13`: if VBR callback sees a real UTLB miss for a VPN already tracked in the software table, reinstall the original slot/way instead of returning success with a still-absent hardware translation. Explicit page hints remain cheap hits. **Does not cure missing emulator exception delivery**.
- `ee0e3ac`: regression in `tests/fault_pager_test.cpp` for that refault condition.

## Next hardware-fault investigation (do not fake success with hints)
1. Rebuild `make clean && make -j4 && make check-pe && make pager-experiment`; run host `tests/fault_pager_test.cpp` and CI. None of these prove emulator exception delivery.
2. Re-test the normal rejection path and PAGERTEST; retain exact four counters and return values.
3. Instrument emulator **at memory-translation failure** (outside NTOS): when AT=1 and UTLB lookup fails on `0x01001400`, does it raise SH4 TLB miss, write TEA, EXPEVT=0x60 (write), save SPC=`0x01000208` and SSR, and jump to VBR+0x400? Or does a generic P0 memory guard abort before exception injection? Compare emulator's supported SH7305 MMU/UTLB model to our `src/mmu.hpp` and `src/tlb_helper.S` (including 4-way/slot encodings). **Must not assert this is definitely an emulator bug until the trap mechanism is observed.**
4. Add *read-only, P1-safe* vector-entry counters/snapshots, recorded before the callback, including TEA, EXPEVT, SPC, SSR, MMUCR; expose only AFTER leaving mapped mode. A separate controlled exception vector smoke test can validate VBR ownership independently of P0 TLB miss delivery.
5. Audit `src/fault_vectors.S` register-bank transitions, original SR, SGR, nested exceptions, forwarding to host general/IRQ handlers, and full teardown. The current vector installation is experimental, not gint-style whole-machine ownership.
6. Once PF>0 and unhinted FAULTTEST returns `0xE0000CA8` with BAD=0, test repeated cycles, pinned re-faults, invalid VA failures and error recovery. Do not test physical CP400 until validated emulator behavior *and* rollback.

## WinCE source study: loader versus pager
Compared uploaded WINCE300, CE4.2, CE5 and CE6 NK/KERNEL/loader.c; `PageInModule`, `PageInProcess`, `LoadE32` and `LoadOneLibrary` appear in all four. These are **not** a drop-in physical MMU exception handler: they coordinate process/module images, page formats, file/ROM sources, relocations, and kernel VM semantics. The attached WINCE300 `LOADER.C` is the smaller baseline; CE6 reorganizes more kernel support outside this file.

**Current NTOS state:** PE graph and import/rebase support, file materialization during initial load (`src/pe_pager.hpp`), entire image physically backed in RAM, and 55-slot UTLB residency cache under experimental flag. Page hints validate mapping/eviction. **Not yet implemented:** safely deferred flash-backed page-in from outside interrupt context, process/private COW versus shared DLL backing, dirty writeback/eviction to backing store, a full WinCE-style process/thread VM model and error statuses, per-page permission enforcement, asynchronous/reentrant exception safety, and full-device interrupt ownership. The loader currently pre-materializes and relocates the entire image before switching AT on; **do not invoke File_Read from VBR+0x400**.

## gint hardware takeover comparison
From uploaded `gint-test.zip`: `src/kernel/world.c` uses driver-world save/restore sequencing; `src/intc/intc.c` saves/restores INTC priority and masks and sets SH4 VBR handler slots; `src/mmu/mmu.c` saves/restores SH7305 PASCR and IRMCR. This is much wider than merely swapping VBR. NTOS `src/host_world.hpp` currently captures SR/VBR/GBR/DBR/MMUCR but only restores selected CPU registers; `src/irq_handoff.hpp` owns VBR transactionally but forwards host IRQs and relies on Hollyhock while outside the hot path. To reach gint-like ownership, introduce per-device CP400-specific state descriptors, ordered quiesce/save/install/restore, INTC priorities/masks, timer/input/display/DMA state, cache and MMU state including safe AT transitions, and robust reversible host return; port only the CP400-backed drivers, not all gint device drivers. The supplied OS_02.01.7002.fls.c and fw3070 are a hardware-register/reference source; verify each field before touching physical hardware.

## Testing/claims
- Commits above were written directly to GitHub `main` via connector.
- No interactive CP400 emulator is accessible in this environment: **FAULTTEST not verified, exception delivery not fixed**. Do not silently replace FAULTTEST with hinted mode.
- CI state must be independently checked for the new commits; do not infer success from previous fdc3e93 build.
