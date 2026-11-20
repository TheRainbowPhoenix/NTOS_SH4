# NTOS Mm: real SH7305 PE page permissions and controlled VBR canary

Development branch, NOT a proven device release. The verified CP400
observations before this patch were:
- PAGERTEST 0xE0000CA8, HINT=160, BAD=0
- LARGE4M 0x4B029400, HINT=1024, EV=970, BAD=0
- CHAINTEST 0x373F373F, no faults
- STRESSEXEC 0x1234004E, no faults
- MMTEST 0x4D4D0060, PF=0, HINT=193, EV=84, BAD=0
- IOTEST 0x10F00010

These validate **explicit pre-faulting**, not the CPU's VBR+0x400
exception trampoline.

## Architecture (conceptually inspired by NeptuneOS Mm)

Like the NT-inspired NeptuneOS architecture, divide responsibilities:
1. **PE loader** parses sections, checks bounds and resolves imports
   while the actual SDK File_Open descriptor is CLOSED before switching
   into guest MMU/VBR ownership.
2. **Mm protection policy** (`src/nt/mm_protect.hpp`) classifies each
   4096-byte page as RO, RW or absent using *all* overlapping sections.
   A mixed writable/code page is necessarily writable.
3. **Memory manager / pager** (`src/fault_pager.hpp`) stores a small
   fixed set of registered P1-backed module regions and tracks residency.
   No SDK I/O, memory allocation, or LCD calls in the fault callback.
4. **HAL** (`src/mmu.hpp`) programs real SH4A PTEL.PR=10 (0x15c,
   user/supervisor RO) or PR=11 (0x17c, user/supervisor RW), preserving
   4096-byte size and valid/cached/dirty flags. SH4A has NO NX bit.
   All normal API window mappings remain RW.
5. **Exception gate** (`src/hardware_fault.hpp`,
   `src/fault_vectors.S`) still needs true hardware-entry validation.
   A VBR+0x100 write-protection exception (EXPEVT=0x0c0) is currently
   forwarded to the host, NOT converted into an NT user exception.
   Deliberate write-to-readonly testing on physical CP400 is forbidden.

NeptuneOS itself targets ARM64/seL4 and is GPLv3. This is an
independent SH4A hardware implementation, informed by its separation
of object manager, memory manager and private platform operations.
No upstream code has been transplanted.

## Fix to PROCTEST chain

Previously a completed serial app launch with no pending successor always
queued `\\fls0\\STARTHOST.EXE`. That file was absent from the P0–P2
test archive, causing a **misleading** "PE file unavailable or filesystem
busy" error *after* `PROCFINAL.EXE` returned 0x70030003.

The host now uses a handle-free `File_Stat` check before scheduling this
optional fallback. If it is not present, the chain finishes cleanly,
retaining the final return value. The phone shell still opens
STARTHOST when it is installed.

## Builds and run order

The CI job outputs TWO incompatible artifacts:

**Physical CP400 candidate (opt-in experiment):**
`NTOS-CP400-MM-PR-RO-RW-EXPERIMENTAL-NO-FAULTCAN`
- Uses `NTOS_ENFORCE_PE_PAGE_PERMISSIONS=1`.
- Includes `MMROTEST.EXE` (expected **0x4D520010**) that queries
  the new Mm API [38], reads RO `.rodata` and writes a separate RW
  `.bss` page. It NEVER writes to RO memory.
- Start with `HELLO.EXE`, then MMROTEST, MMTEST and optionally
  PROCTEST. Check HINT/EV/BAD.
- No `FAULTCAN.EXE` in this package. Kernel API [39] returns -1 if
  called, so an accidental unhinted VBR canary is disabled.

**Emulator-only VBR canary:**
`NTOS-EMULATOR-ONLY-ONE-UNHINTED-VBR-FAULT-CANARY`
- Uses `NTOS_ENABLE_CONTROLLED_VBR_FAULT_PROBE=1` in addition to
  the protected-page gates.
- `FAULTCAN.EXE` deliberately **arms** exactly one expected RAM-backed
  .bss data-write TLB miss, then performs one unhinted write; expects
  **0xFACC0001**, **PF>=1**, **BAD=0** and
  `VBR-CANARY seen=1` after handler return.
- A failed VBR trampoline can hang/reset the emulator. Do not run on
  real CP400 until successful emulator exercise and exception-frame
  register preservation review.
- User-space [39] verifies target is registered, nonresident and RW.
  It NEVER fabricates a fault, allocates memory, or writes to flash.
  The VBR interrupt callback records a matching EXPEVT 0x060 and
  increments seen once; incidental faults are tracked separately.
- A successful one-miss test is only the first step: it does not
  validate protection violations, nested faults or interruption while
  SDK/host world is active.

## ABI appended, backwards-compatible

NtosApi[38] `mm_page_rights(virtual_address)` returns 0=no mapping,
1=RO, 2=RW. This is the manager's **intended** rights, not direct
P4 UTLB readback.

NtosApi[39] `mm_arm_fault_probe(address, expected_expevt)` is enabled
ONLY in emulator-canary firmware; returns -1 otherwise.

NtosApi[40] `mm_fault_probe_seen()` returns matching VBR canary callbacks.
The existing first 38 API slots are unchanged; older WinCE payload
binaries continue to work.

## Still missing for an NT-like memory manager

- A proper exception dispatcher for EXPEVT 0x0a0/0x0c0 with a
  protected guest fault exit or safe guard-page handler, not a blind
  return to a faulting instruction.
- Independent ASIDs, separate protected process contexts and preemption.
- True COW, commit/reserve accounting, exception-safe W^X and
  executable protection (SH4A has no per-page NX).
- Genuine WinCE DllMain attach/detach rollback.
- Flash-backed page eviction or IO from fault path: deliberately NOT
  implemented; image bytes are preloaded into P1 RAM.

Always keep a known-good `NTOS.hh3` and flash backup.
If any filesystem close fails, do not retry; stop NTOS and return to a
clean Hollyhock session. On-device safety is not guaranteed by CI.
