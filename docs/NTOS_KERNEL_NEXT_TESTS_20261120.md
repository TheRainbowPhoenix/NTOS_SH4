# CP400 kernel test order — SH4AL-DSP (NO FPU)

Single matched ZIP. Back up the known-good NTOS.hh3. This candidate exercises a real P4 UTLB associative invalidation and ITLB readback. Do not run on a kit you cannot recover; restore last validated HH3 on reset.

1. `KTHREAD.EXE` => **0x4B540010**, SVC=3 YIELD=1 BADGATE=0.
2. `KEXCEPT.EXE` => **0x4B455801**, PROT=1 USRRET=1.
3. `ASOWN.EXE` => **0x41534F10**, serial single-PID VM root.
4. `PSPARENT.EXE` => **0x50535010**, queues PSCHILD automatically; `PSCHILD.EXE` => **0x50534310**. Child PID is distinct and parent field refers to outgoing process; this is NOT two simultaneous EXEs.
5. `MMREUSE.EXE` => **0x4D444331**: 2-page reserve+commit; attempt release while committed fails; MEM_DECOMMIT 1 page, re-commit returns zero frame, other page unchanged; decommit both + release + reserve at same VA returns zero.
6. `MMGUARD.EXE` => **0x47524401**, PROT=1 REC=1 USRRET=1.
7. `MMREAD.EXE` => **0x52444101**, PROT=1 REC=1 USRRET=1.
8. `MMRDINIT.EXE` => **0x52444931**, PROT=1 REC=1 USRRET=1.
9. `NATIVEVM.EXE` => **0x4E560010**, HINT=5.
10. `LARGE4M.EXE` => **0x4B029400** on the known-good large-image path.

Report exact return code, PF/HINT/EV/BAD, and whether it returns to picker after each. Do not run anything after a hardware hang until restarting with the last known-good firmware.

Milestones remaining: full interrupt-safe SH4AL-DSP context swap across two KTHREADs and independent *concurrent* process page-table root switching. This build does NOT claim hardware multitasking.
