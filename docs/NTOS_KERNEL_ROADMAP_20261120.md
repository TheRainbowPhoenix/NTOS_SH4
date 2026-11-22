# NTOS_SH4 kernel roadmap — SH7305 / SH4AL-DSP (NO FPU)

## Non-negotiable contract
- The Holyhock custom launcher loads `NTOS.hh3` in the **0x8C000000..0x8D000000** PT_LOAD range. CI validates the *stripped ELF segment end* with `tools/check_hh3_launcher_layout.py`, not the file size alone. The experimental profile keeps >=2048 bytes headroom; keep ~4096+ for normal releases. Avoid UI, font and app logic in stage0.
- Single SH4AL-DSP core (~110 MHz). **No FPU.** Preserve banked integer state and optional DSP state (A0/A1+guard, X/Y/M, DSR, MOD, RS, RE). No FPU opcodes. Limit VBR work to P1-backed bounded operations; no SDK/filesystem/heap or display in an ISR. UBC/DBR remains host-owned until a reversible debug driver is implemented.
- No frame reuse before *both* UTLB and ITLB translations are proved absent. No claiming SMP, concurrent PID isolation, user-mode SEH or a complete Windows NT HAL from host-only tests.

## Core executable/boot flow
1. Hollyhock Custom Launcher → NTOS.hh3 **stage0** (verify host MMU off, snapshot VBR/SR/GBR/DBR, install bounded P1 exception stack and VBR).
2. HAL + Ke → Mm maps private P0 guest pages and COREDLL's API window; Ps creates KPROCESS/KTHREAD with unique PID/TID and `InheritedFromUniqueProcessId`; Ob/Ex allocate kernel objects and pool.
3. Ldr maps and relocates PE32 SH4BE EXEs/DLLs, snaps imports, initializes DLLs dependency-first; COREDLL remains userspace API library.
4. Later `SMSS-lite` / `INIT.EXE` coordinates a minimal services + driver launcher, then user applications, without growing the firmware UI.
5. Shutdown: cancel thread exception registrations → stop/wait threads → close handles/files → flush/verify MMU → restore host VBR/SR/DBR and original Hollyhock world.

## Milestone gates and probe matrix
| Gate | Implementation | Experiment | Acceptance |
|---|---|---|---|
| 1b | Per-KTHREAD exact protection record+user recovery | KEXCEPT | **0x4B455801**, PROT=1 USRRET=1; on CP400 confirmed |
| 2a | Owner-checked per-KPROCESS private VM; sequential loader | ASOWN | **0x41534F10**; on CP400 confirmed |
| 2b | Unique child PID and parent link across serial NtLaunch | PSPARENT → PSCHILD | **0x50535010 → 0x50534310**, child != parent; *not concurrent* |
| 2c | Two prepared private roots with same VA but distinct backing | nt_ps_dual_space_test (host) | switching refused without TLB acknowledgement; no cross-PID resolution |
| 3a | No-FPU cooperative register+two-stack hardware proof | KCOOP | **0x4B434F31**, 32 alternating A/B iterations; fibers in one EXE, **not yet KTHREAD** |
| 3b | Two actual KTHREADs in one image: distinct guest stacks + full saved bank0/bank1, PR, GBR, SR/SPC/SSR, DSP state; scheduler selected via TRAPA | **K2THREAD.EXE (next)** | same PID, two different TIDs; 1000 alternating yields, register canaries, no corruption |
| 3c | Two simultaneous loaded PE graphs / KPROCESSes; switch P0 root only after verified invalidation | **PSTWIN.EXE + TWINA.EXE + TWINB.EXE (next)** | distinct PIDs; child parent PID = creator's PID; same VA resolves different physical data; both counters progress |
| 4a | P4 associative UTLB/ITLB invalidation + readback + no-reuse-on-failure | MMREUSE | **0x4D444331**, zero on recommit, untouched neighbor, release after decommit |
| 4b | Stress live decommit/release during thread switches | **MMRACE.EXE (next)** | 500 cycles; never stale PA; no leaks; no unexpected faults |
| 5 | PE import/forwarder/DLL lifecycle under 4MiB+ applications | LARGE4M, STRESSEXEC, CHAINTEST | existing expected codes unchanged |
| 6 | Driver model phase 1: synthetic character device + I/O cancellation/teardown | **DEVNULL.EXE (next)** | IoCreateDevice, CreateFile, ReadFile, WriteFile, DeviceIoControl with deterministic statuses |

## Hot-path architecture boundaries
- `hal/sh7305`: exception vector ownership, UBC debug gate, INT controller, MMU/TLB associative invalidate, cache sync, timer, physical device register adapters.
- `ke`: KTHREAD scheduling, ready queue, waits/events, priorities/quantum, complete integer+DSP context. Start cooperative then introduce timer preemption behind a hardware gate.
- `ps`: KPROCESS, parent ID, process/thread handles and teardown, per-process mapped image graph, process creation and inheritance policy. No inherited PID: each child gets a unique PID; it may inherit selected *handles* later.
- `mm`: per-process VA→PA ownership, reservation/commit/decommit/protect, fixed P1 frame arena, page faults, frame refcounts, ASIDs or full invalidation on switch. Keep `MMUCR.SV=1` in existing baseline until process-switch experiment proves the required mode/ASID semantics.
- `ob`/`ex`: object refcounts, handle tables, tagged pool, synchronization, typed object names.
- `io`: NT-style DRIVER_OBJECT, DEVICE_OBJECT, FILE_OBJECT, bounded IRP, IoCreateDevice, IoCallDriver, IoCompleteRequest; dispatch READ/WRITE/DEVICE_CONTROL first. No I/O in VBR.
- `ldr/rtl` and COREDLL: PE import/relocation, WinCE-compatible userspace ABI, NTSTATUS, structured error flow. Grow COREDLL rather than the 64KiB HH3 stage0.
- `driver/sh7305`: start with a **host SDK proxy** driver for console/keyboard/touch, then native LCD, keyscan, touch controller, flash and USB (each behind separately reversible HAL ownership). No direct GUI/phone shell dependencies in kernel.

## Critical path to useful NT-like kernel
1. Validate MMREUSE and KCOOP on physical CP400; keep KTHREAD/KEXCEPT/MMGUARD/MMREAD/NATIVEVM as regression gates.
2. Turn KCOOP's audited ABI primitive into **Ke-supported** thread creation with owned stack, ready/wait queues, TRAPA yield, no-FPU DSP context, and safe lifecycle. Do not use the preflight fiber as a fake KTHREAD.
3. Load two PE images concurrently in distinct KPROCESS roots; defer simultaneous same-VA mappings until trap-time `MmSwitchAddressSpace` can safely invalidate or switch ASID and the pager rebinds roots. Keep stage0 separate from COREDLL.
4. Validate live decommit across real thread switches; add negative bad-PID/invalid-pointer tests.
5. Driver ABI before GUI: IRP dispatch to memory-backed /dev/null + loopback first, then replace its HAL adapter with board-specific drivers.

## References / adaptation
- NeptuneOS `private/ntos/src/ke, ps, mm, io`: NT naming, object/service dispatch, strict fault ownership and bounded state transitions. NeptuneOS's seL4 mechanism is *not* present here; do not claim equivalent isolation.
- WinCE 3.0 `PRIVATE/WINCEOS/COREOS/NK/KERNEL/EXDSPTCH.C`, `SCHEDULE.C`, `VIRTMEM.C`: loader lifecycle, exception context, TLB invalidate-before-page-reuse and scheduler flow.
- KallistiOS `kernel/arch/dreamcast/kernel/mmu.c`, `thdswitch.s`: SuperH reference, **but Dreamcast SH4 FPU save/restore is wrong for SH7305**.
- gint-exword + TarrasK `breakpoint_handler_stub.S`: SH7305 MMU, banked registers, optional UBC debug trap. UBC is not a substitute for Ke scheduling.
- LiberNT and nt5src: NT API naming/behavior reference only; implement compatible clean-room semantics, respect each source's licensing.

Track production issues separately from device-hazard candidates. Each PR must include host regressions, SH4BE opcode audit when relevant, one matching ZIP, the HH3 RAM segment gate and real-device return codes. Do not merge unproven hardware exception/MMU paths automatically.
