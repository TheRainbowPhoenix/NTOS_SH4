# CP400 kernel test order — SH4AL-DSP (NO FPU)

Single matched ZIP. Back up the known-good NTOS.hh3. This candidate exercises a real P4 UTLB associative invalidation and ITLB readback. This experimental build requires >=1536 free bytes below 0x8D000000; normal builds keep a larger margin. Do not run on a kit you cannot recover; restore last validated HH3 on reset.

1. `KTHREAD.EXE` => **0x4B540010**, SVC=3 YIELD=1 BADGATE=0.
2. `KEXCEPT.EXE` => **0x4B455801**, PROT=1 USRRET=1.
3. `ASOWN.EXE` => **0x41534F10**, serial single-PID VM root.
4. `PSPARENT.EXE` => **0x50535010**, queues PSCHILD automatically; `PSCHILD.EXE` => **0x50534310**. Child PID is distinct and parent field refers to outgoing process; this is NOT two simultaneous EXEs.
5. `MMREUSE.EXE` => **0x4D444331**: 2-page reserve+commit; attempt release while committed fails; MEM_DECOMMIT 1 page, re-commit returns zero frame, other page unchanged; decommit both + release + reserve at same VA returns zero.
6. `KCOOP.EXE` => **0x4B434F31**: two independent P0 stacks, 32 A/B alternating cooperative yields, callee-saved GPR/PR/GBR/MAC/SR restoration. This is a privileged **CPU context experiment**, not a kernel KTHREAD context switch or two simultaneous EXEs.
7. `MMGUARD.EXE` => **0x47524401**, PROT=1 REC=1 USRRET=1.
8. `MMREAD.EXE` => **0x52444101**, PROT=1 REC=1 USRRET=1.
9. `MMRDINIT.EXE` => **0x52444931**, PROT=1 REC=1 USRRET=1.
10. `NATIVEVM.EXE` => **0x4E560010**, HINT=5.
11. `LARGE4M.EXE` => **0x4B029400** on the known-good large-image path.

Report exact return code, PF/HINT/EV/BAD, and whether it returns to picker after each. Do not run anything after a hardware hang until restarting with the last known-good firmware.

Milestones remaining: full interrupt-safe SH4AL-DSP context swap across two KTHREADs and independent *concurrent* process page-table root switching. This build does NOT claim hardware multitasking.

### Failure codes for MMREUSE v2
- `0xBAD9E007`: decommit failed, page metadata/counter wrong
- `0xBAD9E008`: recommit or software prefault failed
- `0xBAD9E091`: byte 0 of recommitted frame not zero
- `0xBAD9E092`: byte 4095 of recommitted frame not zero
- `0xBAD9E093`: neighbor page lost its 0x5A value
- `0xBAD9E00A..0D`: later decommit/release/re-reserve/zero check
- `0x4D444331`: success

The PSPARENT→PSCHILD probe now returns to the picker rather than launching STARTHOST.

### Gated 500-cycle stress
`MMSTRESS.EXE`: **0x4D535431** after 500 decommit/recommit cycles and independent second-page data verification. **Do not run MMSTRESS until the corrected MMREUSE returns 0x4D444331 on real hardware.** Stress is one foreground KTHREAD in this revision; a true cross-thread MMRACE remains gated on the real Ke context switch.

### Ke-managed two KTHREAD experiment
`K2THREAD.EXE`: expected **0x4B325431** for two kernel-owned TIDs alternating 1000 times, preserving integer ABI state and SH4AL-DSP register words. This is **privileged cooperative** switching (not arbitrary user-mode/RTE preemption and not two EXEs). Use the new API slots [67..70], which do not alter old slots. Test KCOOP and KTHREAD first. On a reset restore known-good firmware. This isolated firmware gate requires >=512 bytes below the hard HH3 loader limit.

### MMRACE: actual 2-KTHREAD live-decommit stress (gate 3)
Run **only** after corrected `MMREUSE.EXE` returns `0x4D444331` and `K2THREAD.EXE` returns `0x4B325431`. `MMRACE.EXE` executes 500 alternating decommit/recommit cycles **per KTHREAD** using separate P0 private pages in the same KPROCESS. Expected **0x4D524335** (1,000 total successful decommits); every round verifies zero-fill and that the other thread's page contents survive. A `0xBADADxxx` result identifies a failed stage; report exact code, TEA, EV, PF, BAD and return-to-picker. This is not two simultaneous separate EXE graphs or preemptive scheduling.

### New NT device I/O acceptance
The NtosApi extension [71..75] is `NtOpenDevice(name,&handle)`, `NtReadDevice(handle,P0_buffer,bytes,&transferred)`, `NtWriteDevice(...)`, `NtCloseDevice(handle)`, and `NtDeviceIoControl(...)`. Data/output words must be resident P0; P1/P4 pointers are rejected. The first two drivers are kernel-owned `\\Device\\Null` and `\\Device\\Loopback` — not real hardware. `DEVNULL.EXE` expected **0x444E4C31**; `DEVLOOP.EXE` expected **0x444C5031**. Invalid-buffer, stale handle and unsupported IOCTL fail by NTSTATUS. No SDK descriptors are opened. Keep K2THREAD, MMRACE and MMREUSE as regression gates. Real concurrent two-EXE process address space switching is a future hardware gate, not claimed by these tests.

### Two independently linked PE EXEs resident and alternating (NOT process-isolated yet)
With `PEPAIR.EXE` launched from picker, stage0 preloads **`PEPEER.EXE`** from flash while the host MMU/VBR is still restored. The child has an independent PE image, different P0 executable base (rebased to ~0x00500000), and separate code/data/backing; the parent and peer alternate **250 times** using Ke cooperative context switching and an explicit P1 progress service. Expected parent **0x54575031** and peer internal **0x54574331**. The peer doesn't appear as a serial launch because it runs before the parent returns. Both currently share **one KPROCESS PID and one P0 mapping**, so this does NOT establish two isolated KPROCESSes or independent parent/child PIDs. The following milestone must switch guest roots under verified UTLB/ITLB handoff before assigning distinct hardware-running PIDs. Run DEVNULL and DEVLOOP first, then PEPAIR; on any reset restore last known-good HH3.

### PSROOT / PSTWIN first *real* P0 data-root handoff
- **PSROOT.EXE expected `0x50535231`**. Run FIRST. Stage0 creates exactly two aligned P1 data frames and exposes one pinned P0 VA at NtosApi [79]. [80] switches backing PA after P0 alias cache writeback + verified associative UTLB/ITLB invalidation, P4 readback and verified reinstatement; [81] reports 0/1. PSROOT flips 500 rounds each direction, checks first/last words and preserved values. Abort on ANY BAD 0xBADC100x / PF / TEA. **Never run PSTWIN if PSROOT fails.**
- **PSTWIN.EXE expected `0x50535431`**, automatically loads **TWINCHILD.EXE** (internal `0x50534332`; don't launch separately). Two independently linked PE images at DISJOINT executable VAs alternate 500 Ke KTHREAD yields, with separate logical Ps PIDs, child PPID, separate live P0 DATA backing at SAME VA, TLB ack before each context switch and PID publication; the child must not close the parent's NT Io handle. APIs [55], [64], [66], [67-70], [76-82] exercised. **This is bounded mixed-root execution, NOT full two-KPROCESS same-VA code/stack graphs, timer preemption, or independent COREDLL instances.**
- Regression: K2THREAD, MMRACE, DEVNULL, DEVLOOP, KEXCEPT should retain proven codes. New hardware root needs explicit acceptance before merging.

### #24 and #27 new hardware canaries
- **KREGCTX.EXE expected `0x4B524731`**. Two genuine cooperative kernel KTHREADs, 500 yield pairs, assembly saves/verifies r8–r12 plus SH4AL-DSP X0/Y0/M0 with distinct per-thread seeds while restoring original DSP and r8–r14. No FPU opcodes. This does **not** complete full banked-register IRQ preemption/RTE.
- **DEVREPL.EXE expected `0x44525631`**. Guest calls NT `IoReplaceDriver` diagnostic API slot [83] for `\\Device\\Loopback`. Switch while file open must return `0xC00000E8`; after close substitute two kernel DRIVER_OBJECTs, verify plain vs inverted read bytes and transfer status. Both are *synthetic* drivers; real SH7305 display/keyboard/touch/USB implementations are still a separate gate.

### Hardware-gated next #24/#25/#27 bundle — PSCODE, KTRAPREG, DEVDISP
- **PSCODE.EXE expected 0x50534344**: preloads `CODEA.EXE` and `CODEB.EXE` before VBR ownership. These are **two separately linked SH4BE PE images**, with independent P1 text + initialized and uninitialized data. They are *rebased to exactly the same P0 text/data virtual address*, which is selected by API [84]. 500 calls each execute different machine code and preserve independent .bss counters. Each root flip is **one IRQ-masked transaction** of all PE executable pages plus the private PSROOT page with P0/P1 cache writeback, I-cache purge, verified UTLB+ITLB invalidation and P4 readback. On failure restore and verify all original mappings; fail closed. This is *not* a whole-process code/stack/MMU root switch or timer preemption. **Never launch CODEA/CODEB alone.** Run after PSROOT and PSTWIN regressions.
- **KTRAPREG.EXE expected 0x4B545231**: 128 real bank0-r1–r7 and SH4AL-DSP X0/Y0/M0 canaries across privileged→user TRAPA #0x7A→P1 VBR+0x100→RTE, verified service PID and exact syscall count. No FPU. This validates **actual interrupted register preservation** but does not turn on timer-based dispatch, user-mode arbitrary PC or complete preemptive two-thread RTE.
- **DEVDISP.EXE expected 0x44535031**: opens `\\Device\\Display` through guest NtOpenDevice, passes a 12-byte x/y/RGB565 command (IOCTL 0x4E540001) to a real CP400 framebuffer kernel driver, and tests out-of-range denial, unsupported IOCTL and close. Its backend reuses the proven `setPixel` HAL framebuffer operation, **not native LCD MMIO/IRQ**. The probe paints a tiny pixel and resets it to white.
- Regression gate: first KTHREAD, PSROOT, PSTWIN, KREGCTX, DEVREPL, then PSCODE, KTRAPREG, DEVDISP, finally MMREUSE/MMRACE. Stop on any BAD code, crash or unexpected PF/EV. Draft only until hardware returns exact codes.

### #33: first real P0 user-stack loop (PSUSER, opt-in)
- **PSUSER.EXE expected `0x50535531`**, BADF510x on failure; reads NtosApi [85] 4 KiB P0 user stack, [86] exact two TRAPA instruction locations, [87] 128 counted user syscall dispatches. In privileged loader, a P1 frame is allocated before MMU takeover and pinned read-write at 0x0071xxxx P0. The SH4BE assembly switches r15 from privileged P1 to actual P0 user stack, clears SR.MD while masking IRQ, and performs 128 ordinary user-mode PUSH/POP and arithmetic groups, plus TRAPA #0x7A. The P1 VBR Ke dispatcher checks SSR.MD=0, user SGR inside P0 stack, exact instruction PC, actual CPU TRA, bounded count, syscall ID, PID, and CPU-generated RTE state; final dedicated 0x10FF trap restores privileged SR so the user stub can restore saved P1 SP and C ABI. No FPU/SDK/heap from VBR.
- **Safety**: PSUSER is an explicitly armed bounded *single-KPROCESS* canary, NOT general arbitrary Win32 user processes, real exception termination, timer preemption or safe user-pointer dereferences. **Run PSROOT and KTRAPREG first**. If unexpected fault/reset, stop and revert to proven HH3. Follow-up #33 remains PSUSER validation → two whole Ps-owned roots / PID+handles+VM+stack → preemption and native HAL.

### #33: PSKILL process-lifetime / Io owner reaping
- **PSKILL.EXE expected `0x50534B31`**. Automatically preloads **KILLCHILD.EXE**. Ke creates two distinct KTHREADs linked to distinct logical Ps PIDs, with private same-VA physical data remap, performs 500 child-parent yields, and child deliberately leaks \\Device\\Null handle. Ke child exit runs `IoManager::close_owner(child_pid)` under P1 Ke after verified root handoff, then clears child KPROCESS active state; parent \\Device\\Loopback handle remains valid. NtosApi [88] returns bounded global NT Io handle count (0→1→2→1→0). Internal child success code `0x4B4C4331`; **do not launch KILLCHILD alone**. This proves per-PID Io cleanup under cooperative Ke switching; NOT general two Ps Process tables/private P0 stacks/independent VM roots, which remain in #33.

### Next SH7305 native P2 driver gate — HALTMU (gint-exword fxCP reference)
- **HALTMU.EXE expects `0x544D5531`**; stage errors `0xBADF610x`. Kernel enables `\\Device\\Timer` (NT DRIVER_OBJECT + DEVICE_OBJECT + IRP) whose sole supported `IOCTL=0x4E540100` copies six uint32_t words (magic, TMU0 TSTR, TMU0 TCOR, TMU0 TCNT, TMU0 TCR, INTC IPRA) read **directly from native SH7305 P2 MMIO** at addresses validated against `TheRainbowPhoenix/gint-exword` `include/gint/mpu/tmu.h`/`intc.h`. READ-ONLY: no timer/interrupt configuration, no write, no SDK use under VBR. Runs only in privileged P1 normal Ke context; guest buffer and PID-scoped handle are checked; invalid sizes and codes denied. This is the first native timer register-backed NT Io device, **not** preemption, and does not touch the HIOS timer state. Run after PSROOT/KTRAPREG/PSUSER and before PSTWIN/PSKILL regressions. Never confuse this passive readback with #24 complete.
- `NTOS_PROCESS_HEAP_RESIDENT=1` moves foreground `nt::Process` (Ob/Ex/Ke/VM/handle tables) out of HH3 static memory to P1 heap, allocated before VBR/MMU guest ownership and freed only after host restore. All existing regressions must pass; verify no launch-loop leak and exact required 1 KiB HH3 guard. Only the selected experimental CI profile enables this.
