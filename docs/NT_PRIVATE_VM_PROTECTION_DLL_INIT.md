# NTOS next-step VM, guarded exception and DLL initializer diagnostics

Development branch on top of the **unmerged** PE-protection/VBR canary PR.
This is not a general-use OS upgrade or completed NT process subsystem.

## Observed earlier hardware/emulator milestones

- User reported a real unhinted VBR TLB write miss: FAULTCAN
  `PF=1 HINT=0 EV=0 BAD=0`, `TEA=00450000 EXPEVT=060`,
  `VBR-CANARY seen=1 other=0`, `0xFACC0001`, clean host return.
- 4 MiB streamed PE image: `0x4B029400`, `HINT=1024 EV=970 BAD=0`.
- Serial PROCTEST: two deliberate outgoing resource leaks recovered with
  `handles=2 ExBytes=8192`, then `handles=2 ExBytes=4096`, final
  `0x70030003`, launcher return.
- Earlier `MMTEST.EXE` returned `0x4D4D0060`, `HINT=193 EV=84 BAD=0`.
  **That is MMTEST, not the MMROTEST expected `0x4D520010`.**
  RO hardware PTE test should be run separately.

## Design

In the same *separation-of-responsibilities spirit* as NeptuneOS's
NT Executive atop seL4 (https://github.com/cl91/NeptuneOS), NTOS keeps:
- Ob/Ex/Ps ownership in `src/nt/process.hpp` and its private resource tables
- `src/nt/virtual_memory.hpp` for an explicit 256 KiB **P1-backed**
  64-page/8-region private VM arena
- `src/fault_pager.hpp` for P0 VA-to-P1 frames + 55-slot residency and
  synchronous PTEL.PR reinstall on NtProtect
- `src/nt/guest_exception.hpp` for a one-shot, page- and-PC-validated
  read/write SH4A protection fault decision, and
  `src/hardware_fault.hpp` / `src/fault_vectors.S` for the gated
  VBR+0x100 P1 exception stack, exact register save/restore and SPC update
- `src/nt/dll_lifecycle.hpp` for dependency-first DLL_PROCESS_ATTACH,
  exact LIFO DLL_PROCESS_DETACH and rollback on FALSE

This is original SH7305/SH4A-specific implementation, not a code
copy, and does NOT imply NeptuneOS's multiple address spaces,
preemption or seL4 isolation are available.

## Opt-in API slots appended after NtosApi[40]

| Index | Signature | Contract |
|---|---|---|
| 41 | `uint32_t vm_reserve(uint32_t preferred, uint32_t pages)` | Return page-aligned P0 base or 0; preferred=0 selects first free VA in 0x06000000..0x06ffffff |
| 42 | `NTSTATUS vm_commit(uint32_t base,uint32_t pages,uint32_t rights)` | rights=1 RO, 2 RW; zero-fill distinct preallocated P1 frames; no SDK I/O |
| 43 | `NTSTATUS vm_protect(uint32_t base,uint32_t pages,uint32_t rights)` | Atomically update metadata and resident SH4A PTEL.PR; rollback on error |
| 44 | `uint32_t vm_query(uint32_t va)` | 0 absent/reserved, 1 committed RO, 2 committed RW |
| 45 | `NTSTATUS vm_release(uint32_t base)` | Only for a region without committed pages; prevents unsafe live UTLB alias reuse |
| 46 | `uint32_t vm_committed_pages(void)` | Per-launch count, max 64 |
| 47 | `NTSTATUS arm_guest_guard(uint32_t tea,uint32_t expected_expevt,uint32_t recover_pc)` | Emulator-only opt-in; exact resident RO page, write event 0x0c0, executable PE continuation |
| 48 | `uint32_t guard_seen(void)` | Count of one-shot accepted general protection faults |
| 49 | `uint32_t guard_status(void)` | `0xC0000005` after accepted protection violation |
| 50 | `uint32_t guard_fault_address(void)` | Last accepted TEA |

This is a **private compact NtosApi** rather than fully ABI-compatible
Windows NtAllocateVirtualMemory/NtProtectVirtualMemory. The latter
will eventually require process handles, 32-bit in/out pointers,
allocation types, old protection and status translation.

VM caveats: MEM_RESERVE without MEM_COMMIT has no P0 physical mapping.
MEM_COMMIT allocates from a fixed P1 arena allocated **before** any VBR
handoff. Live decommit, committed-region release, pagefile writeback,
ASIDs/COW and separate address spaces are intentionally unsupported.
No newly committed frame is used as a file-backed pager target.
The whole arena is reclaimed ONLY after MMU/TLB disable and host restoration.

## Physical calculator — safe-intent experimental ZIP

`NTOS-CP400-PRIVATE-VM-DLL-LIFECYCLE-EXPERIMENTAL-NO-MMGUARD`

Backup the existing working `NTOS.hh3`. Install the **matching**
candidate and matching EXEs and DLLs into `\\fls0\\`. Run in order:

1. `HELLO.EXE`: return `0xC0FFEE42` and launcher.
2. `MMROTEST.EXE`: return **`0x4D520010`** (not
   `0x4D4D0060`), `BAD=0`. No illegal write.
3. `VMTEST.EXE`: return **`0x564D0010`**,
   `HINT=8`, `BAD=0`. Creates a 12-page reservation,
   commits 8, writes data, reprotects first 4 RO, reads them,
   restores RW. Live committed-frame release must be refused.
4. `INITPASS.EXE` + `INITGOOD.DLL`: return
   **`0xD11A0001`**. With trace enabled, expect
   `DLL init: attach=1 detach=1 rejected=0`.
5. `INITFAIL.EXE` + `INITFAIL.DLL` + `INITGOOD.DLL`:
   **expected clean loader error** `DLL_PROCESS_ATTACH returned FALSE`,
   NOT an EXE return code. Trace must show
   `DLL init: attach=1 detach=1 rejected=1`.
   The EXE's `0xBAD91003` indicates a serious ordering/rollback bug.
   Re-run INITPASS and OBTEST afterward to prove recovery.
6. Optionally PROCTEST (full three-EXE chain, `0x70030003`),
   PAGERTEST, and IOTEST to verify no regression.

The bundle contains NO deliberate illegal-write payload and does
not enable the new general-protection exception vector handler.

## Emulator-only protected-write ZIP

`NTOS-EMULATOR-ONLY-EXPEVT-0C0-RECOVERABLE-GUEST-TRAP`

Run **MMGUARD.EXE only with the matching emulator-only NTOS.hh3**:
- First hints its RO PE page, then arms one exactly matched
  `EXPEVT=0x0C0` write-protection fault at that page and performs
  one intentionally illegal write.
- The P1 VBR+0x100 callback validates SSR.MD=0 (P0 guest),
  the expected TEA page and event, both fault SPC and recovery SPC
  within PE executable sections, and atomically disarms the guard.
- It sets the saved privileged SPC to a known in-function recovery
  label. RTE returns without retrying the illegal instruction.
- Expected `Payload returned 0x47524401`,
  `PROT=1 REC=1 UNEXP=0`, `BAD=0`. Normally
  `PF=0 HINT=1` for this isolated test.
- A hang/reset or any `BAD930xx` is a failure; do NOT repeat on
  real hardware. An unmatched general exception is forwarded to the
  previous host VBR+0x100 rather than guessed at or skipped.

The dispatcher recognizes both SH4A protection event codes
`0x0A0` (read) and `0x0C0` (write); however the current kernel
has no P0 user-inaccessible-but-kernel-readable mapping to generate
a safe **live read** violation. Read event decision logic is host
tested; its physical trap path is NOT claimed as validated.

## What CI proves and does NOT

CI compiles three distinct SH4BE HH3 profiles, creates all matching
PEs, runs VM allocation/rollback and 55-way pager host tests,
one-shot guest exception decision tests, actual DLL graph-order
and rollback host tests, SDK flash fail-stop regressions, console,
and 4MiB streamed parity. It does not emulate SH4A PTEL/EXPEVT
microarchitecture or execute guest DLL entry on physical hardware.

NEVER inject ambiguous SDK close errors on a real calculator. NTOS
holds at most one SDK file/finder descriptor and poisons its gate
permanently after a failed close—never retrying or reopening the
possibly recycled raw descriptor.
