# CP400 real-device protection-W fix and first NT native VM interface

This is an **unverified runtime candidate** stacked on PR #17, which is
itself not yet merged. Do not confuse successful host CI with real
SH7305 VBR recovery.

## Last device evidence

- `MMREAD.EXE`: **0x52444101**, `PROT=1 REC=1 UNEXP=0 USRRET=1`
- `MMRDINIT.EXE`: **0x52444931**, same four counters
- `MMGUARD.EXE`: the calculator's original host exception handler
  displayed **PROTECT (W)** at target `00401000`, PC
  `0040026E`, followed by reboot
- All non-faulting `MMROTEST`, `VMTEST`, DLL init/rollback, process
  cleanup, Ob, HELLO and large-PE tests were observed as passing

The one-shot VBR+0x100 trampoline is therefore *physically verified*
for a user-mode **read** protection event (0x0A0), but not yet for
the earlier compiler-generated **privileged write** instruction.

## Fix: write through the now-proven user-mode trap mechanism

Old `MMGUARD` armed EXPEVT=0x0C0 and wrote a byte directly from the
compiler's privileged C code. The user-mode read test, by contrast,
entered P0 user mode using a small register-only SH4BE stub and
returned via an exactly validated TRAPA #0x7B. Reuse that *same*
validated path for the write:

1. `MMGUARD` explicitly hints its real PE `.rodata` page, verified
   to be RO by both the page policy and hardware PTEL readback.
2. Before arming, the guarded P1 API checks the original foreground
   guest SR (MD=1, RB=BL=0), an executable recovery PC and the exact
   `TRAPA #0x7B` + `RTS` opcodes.
3. It records a one-shot EXPEVT=0x0C0, exact fault TEA and recovery
   at the reserved TRAPA. It rejects ALL direct privileged C write
   recovery labels in this profile; no arbitrary guest PC can be
   promoted to privileged mode.
4. `payload/user_write_probe.h` clears SR.MD/RB/BL, sets IMASK=15,
   and performs one SH4BE `mov.b r0,@r4` to the protected page.
   The stub does not push a stack frame, call SDK code or access a
   filesystem descriptor while user mode is active.
5. On genuine 0x0C0, the +0x100 handler checks TEA, SPC, event,
   saved mode and executable recovery PC, redirects SPC to TRAPA.
6. TRAPA generates 0x160 with TRA=0x1EC. An exact expected user
   session restores the original privileged SR and RTE returns to
   RTS and then the caller. The protected original value must be
   unchanged, and the event must have incremented `PROT/REC`.

GitHub CI disassembles/checks the **linked SH4BE** `MMGUARD` stub,
not merely the C source. This is an informed fix, not a verified
physical-device pass yet.

## Added process-local NT native VM calls

These are appended ABI slots, preserving all earlier API pointers:

| Slot | Interface | Contract |
|---|---|---|
| 51 | `NtAllocateVirtualMemory(ProcessHandle, BaseAddress*, ZeroBits, RegionSize*, AllocationType, Protect)` | `ProcessHandle=-1`, `ZeroBits=0`, `MEM_RESERVE=0x2000`, `MEM_COMMIT=0x1000`, `PAGE_READONLY=2`, `PAGE_READWRITE=4`; rounds byte region size to 4 KiB |
| 52 | `NtProtectVirtualMemory(ProcessHandle, BaseAddress*, RegionSize*, NewProtect, OldProtect*)` | synchronous PTEL.PR update and rollback, returns old NT protection in writable guest memory |
| 53 | `NtFreeVirtualMemory(ProcessHandle, BaseAddress*, RegionSize*, FreeType)` | `MEM_RELEASE=0x8000` requires region size 0; **only reserved, not committed** pages can be freed |

All return normal NTSTATUS values: SUCCESS=0, INVALID_PARAMETER=
0xC000000D, DEVICE_BUSY=0xC00000E8, etc. IN/OUT pointers must be
aligned 32-bit words in *already resident, writable P0 guest memory*,
as verified against the current Mm page table and UTLB residency.
P1 host/SDK stack pointers, RO pages and unmapped VA are rejected
without being dereferenced. There are no cross-process handles, page
files, demand I/O, ASIDs or preemptive thread support yet. This is
a small native ABI layer, **not** a complete Windows NT memory manager.

`NATIVEVM.EXE` uses static guest P0 globals, explicitly page-hints
their address before calling the Native APIs, commits and reads
two RAM pages, toggles RO/RW and confirms old PAGE protection,
refuses MEM_RELEASE on live committed pages, releases a second
reserved-only region and rejects a P1 stack OUT pointer.

Expected `NATIVEVM.EXE`: **0x4E560010**, `HINT=5 PF=0 BAD=0`.
User-space expected error codes `0xBAD970xx` are failures.

## Artifact and device testing order

The CI produces two real-CP400-labelled profiles. Always use
matching `NTOS.hh3` and matching test EXEs from the SAME archive.

**Non-destructive default candidate**:
`NTOS-CP400-PRIVATE-VM-DLL-LIFECYCLE-EXPERIMENTAL-NO-MMGUARD`

1. `HELLO.EXE`: 0xC0FFEE42
2. `PROTSTATE.EXE`: 0x50520001 (privileged foreground)
3. `MMROTEST.EXE`: 0x4D520010, BAD=0
4. `VMTEST.EXE`: 0x564D0010, HINT=8, BAD=0
5. `NATIVEVM.EXE`: 0x4E560010, HINT=5, PF=0, BAD=0
6. `INITPASS.EXE`: 0xD11A0001
7. `INITFAIL.EXE`: clean DLL_PROCESS_ATTACH returned FALSE; then
   re-run HELLO and OBTEST for teardown.

**Separately gated risk candidate**:
`NTOS-CP400-CONTROLLED-VBR-PROTECTION-TRAPA-REBOOT-RISK`

Only with a known-good HH3 backed up, a cleanly restarted NTOS
session and the correct preflights above, the optional deliberate
fault tests are:

| EXE | Expected return | VBR counters |
|---|---|---|
| MMGUARD.EXE | **0x47524401** | `PROT=1 REC=1 UNEXP=0 USRRET=1 BAD=0` |
| MMREAD.EXE | 0x52444101 | `PROT=1 REC=1 UNEXP=0 USRRET=1 BAD=0` |
| MMRDINIT.EXE | 0x52444931 | `PROT=1 REC=1 UNEXP=0 USRRET=1 BAD=0` |

Read tests have **already passed on physical CP400** on the prior
candidate; this new archive specifically retests the same shared
handler after adding write support. Do not call MMGUARD a pass until
real device results show **PROT=1 REC=1** and clean launcher return.

IMPORTANT: If an OS PROTECT screen or reboot occurs, stop testing
that candidate. Do not run any repeated fault probes after a
reset without restoring known-good firmware and reviewing the trace.
Do not run hardware SDK File_Close fault injection: failed close
must poison the single-owner gate forever, with **zero retries**.

## Next significant NT steps

- A real **KTHREAD / KPROCESS context** with exception-frame tracking,
  scheduling point and user-vs-kernel execution policy.
- Proper user-mode syscall/trap entry for process/thread objects,
  not only one isolated test instruction.
- NT exception records plus per-thread dispatcher to return
  STATUS_ACCESS_VIOLATION to the right process without resetting OS.
- Safe UTLB/ITLB invalidation with shootdown proof, then live
  MEM_DECOMMIT, MEM_RELEASE of committed memory and ASID separation.
- WinCE/NT import support, Object Manager namespace and waitable
  synchronization beyond the currently tested event/handle subset.
