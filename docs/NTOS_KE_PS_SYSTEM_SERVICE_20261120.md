# NTOS CP400 – Ke/Ps user-kernel syscall milestone (single all-in-one kit)

One matching `NTOS.hh3` plus ALL generated PE32/SH4BE EXEs and DLLs,
including COREDLL.DLL. Put relevant files in \\fls0\\ together, preserving
matching versions. Keep your known-good NTOS.hh3 backup.

## Test table

| EXE | Expected result |
|---|---|
| HELLO.EXE | 0xC0FFEE42 |
| PROTSTATE.EXE | 0x50520001 |
| MMROTEST.EXE | 0x4D520010, BAD=0 |
| VMTEST.EXE | 0x564D0010, HINT=8 BAD=0 |
| NATIVEVM.EXE | 0x4E560010, HINT=5 BAD=0 |
| KTHREAD.EXE | **0x4B540010, KE SVC=3 YIELD=1 BADGATE=0, PF=0 BAD=0** |
| MMGUARD.EXE | 0x47524401, PROT=1 REC=1 UNEXP=0 USRRET=1 BAD=0 |
| MMREAD.EXE | 0x52444101, PROT=1 REC=1 UNEXP=0 USRRET=1 BAD=0 |
| MMRDINIT.EXE | 0x52444931, PROT=1 REC=1 UNEXP=0 USRRET=1 BAD=0 |
| INITPASS.EXE + INITGOOD.DLL | 0xD11A0001, attach=1 detach=1 |
| INITFAIL.EXE + INITFAIL.DLL + INITGOOD.DLL | clean DLL_PROCESS_ATTACH returned FALSE; no EXE entry |
| OBTEST.EXE | 0x0B0B0010 |
| PROCTEST.EXE + PROCNEXT.EXE + PROCFINAL.EXE | final 0x70030003 |
| LARGE4M.EXE | 0x4B029400 HINT=1024 EV=970 BAD=0 |

All binaries share a SINGLE opt-in +0x100 VBR kernel. The MMGUARD,
MMREAD and MMRDINIT tests deliberately generate genuine protection
exceptions, despite passing on previous matching real-CP400 builds.
If unexpected reset/protect screen, restore the known-good firmware.

## Architecture

Original SH7305/SH4AL Ke/Ps subsystem, separated by responsibility
after NeptuneOS's NT Executive architecture (see
https://github.com/cl91/NeptuneOS/tree/master/private/ntos/):
- `src/nt/ke.hpp`: KPROCESS, KTHREAD, KTRAP_FRAME, one active
  foreground KTHREAD, process/thread IDs, CPU trap state, service
  dispatch and cooperative yield counter
- `src/nt/process.hpp`: Ps ownership of per-launch Ob handles,
  Ex pool, events, file descriptor gate, VM arena and Ke context
- `src/hardware_fault.hpp`, `src/fault_vectors.S`: HAL-only
  VBR+0x100 trap entry, preserves guest r4/r5 from **bank0** via SH4A
  STC r4_bank/r5_bank, dispatches in privileged P1 with no SDK I/O,
  writes result to guest bank0 r0, restores supervisor SSR, RTE
- `payload/user_syscall_gate.h`: assembly transition to user-mode
  TRAPA #0x7A with service index in r4 and argument in r5, no user
  stack/SDK operations, returns via validated privileged RTS
- `COREDLL.DLL` remains a normal WinCE PE DLL with the existing
  import/export and process initialization machinery. The NTOS
  one-foreground-process loader and first 54 API slots are preserved.

### Ke system services (real SH4A TRAPA #0x7A / EXPEVT=0x160)

| Service ID (user r4) | Result (user r0) |
|---|---|
| 0x1000 | current unique process ID |
| 0x1001 | current unique thread ID |
| 0x1002 | NtYieldExecution -> STATUS_SUCCESS (0) |

One-shot `KeArmSystemCall` (NtosApi [54]) requires the EXACT
TRAPA #0x7A and immediately following RTS at an executable guest
PC. The VBR handler checks the live current KTHREAD, SSR.MD=0,
TRA=0x1E8, exact post-trap SPC and the expected bank0 service index.
It never treats unrelated 0x160 interrupts or host exceptions as
syscalls; otherwise forwards to the saved OS handler.

New optional ABI slots 54–57: KeArmSystemCall, PsGetCurrentProcessId,
PsGetCurrentThreadId, KeSystemServiceCount. ABI slots 0–53 are
unchanged, including full WinCE COREDLL and native VM calls.

**Limitations:** This is ONE foreground KTHREAD and a safe register
transition for three small Ke syscalls, not yet a general user-mode
thread with private user stack/TEB, preemptive scheduling, multiple
processes/address spaces, arbitrary syscall pointer marshalling,
multithreaded synchronization or hardware ASID management. Yield
records a dispatch point; it does not fake a second runnable thread.
Follow-on milestones: user stack/TEB and full KTHREAD CONTEXT save,
scheduler/dispatcher, user-thread ring protection and fault delivery,
safe UTLB shootdown/decommit and per-process isolation.

No VBR path opens/retries an SDK file. A failed or ambiguous SDK
File_Close permanently poisons the single-owner filesystem gate.
