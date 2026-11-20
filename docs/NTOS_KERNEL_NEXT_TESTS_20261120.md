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
