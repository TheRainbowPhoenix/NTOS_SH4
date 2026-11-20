# Ps/Mm dual-EXE, same-VA process isolation — hardware gate

## Current evidence

- K2THREAD (0x4B325431): verified real Ke-owned cooperative KTHREAD switching with SH4AL-DSP register save and no FPU.
- MMREUSE (0x4D444331) and MMRACE (0x4D524335): verified P0/P1 writeback, associative invalidate and readback on one kernel-owned VM root during 500 cycles per KTHREAD.
- DEVNULL (0x444E4C31) and DEVLOOP (0x444C5031): verified guest to kernel Io objects/IRPs. PEPAIR target 0x54575031 still needs exact eight-digit return confirmation.
- `ProcessSpaces<2>` now tracks independent PE section backing plus VM frames, same VA mapped to different PAs across distinct Ps-owned PIDs, and explicit shared RO COREDLL, but it is **host-only**. No verified hardware cross-root switch in this PR.

## Sequence for the first actual hardware-isolated dual process gate

1. **Loader**: before VBR takeover, load *both* PE graphs into independent 4KiB-aligned P1 backing; validate both full PE graphs and module import fixups while host SDK/FS is usable. Shared COREDLL backing can be RO, never RW. Reserve all per-process P0 guest VAs; each graph may reuse the same 0x00400000 base. Disallow API-window collisions and any shared writable physical page.
2. **Ps**: allocate exactly two KPROCESS objects with distinct, nonrecycled PIDs; child `inherited_from_process_id` references the parent's PID. Give each its own resource table, private VM arena, object handles and at least one KTHREAD; initialize the child with a live parent.
3. **Mm**: suspend guest execution in P1 before mapping changes. With interrupts masked, write back and invalidate each live P0 alias **before** invalidating translations. Prove every old process-owned UTLB/ITLB entry absent by P4 readback (including instruction aliases); retain API window, host exceptions and VBR ownership. The CPU has **no FPU** and MMUCR.SV may ignore ASIDs: do not depend on ASID alone. Flush P0 I-cache synonym before executing same virtual code address from distinct PA.
4. **Ke+Ps publish**: only after *full* successful TLB/cache handoff set active PID, active pager image graph and private VM resolver, and saved KTHREAD context. An unsuccessful acknowledgement must leave the old PID and root current, prevent second thread execution, and preserve every old physical frame.
5. **Execution**: two independently linked EXEs each run 500 alternating increments, start at the *same virtual code address* but distinct physical text/data backing. Each calls `PsGetCurrentProcessId` to see its own PID, and child checks `PsGetInheritedFromProcessId == parent PID`. No P1/P2/P4 or host SDK pointer accepted. No SDK file I/O or heap allocations while VBR is owned.
6. **Shutdown**: return to primary root, drain both dispatch queues, cancel/finish IRPs, close each PID's handles, detach both DLL graphs, tear down TLB and cache aliases before freeing either VM/image backing. Return to picker. No automatic STARTHOST.

## Hardware acceptance gate

`PSTWIN.EXE` with separately linked `TWINA.EXE` + `TWINB.EXE`: two different PIDs, distinct physical pages at the *same* P0 VA, correct inherited-from PID, 1,000 total context switches, zero unexpected fault/bad service, all handles and frames released. Must run prior to claiming Priority #25 complete. A separate no-user-SDK driver gate follows for SH7305 display/keyboard/touch/USB.

## Safety limits

Never pretend two resident PEs at disjoint VAs (PEPAIR) are two isolated processes. Don't install an untested TLB purge into production firmware. Keep the dedicated HH3 image under 0x8D000000, prefer >=4 KiB headroom. New hardware test must have distinct return codes for same-VA backing mismatch, PID mixup, wrong parent, denied invalidation, context failure and teardown failure.
