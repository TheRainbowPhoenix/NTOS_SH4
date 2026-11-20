# NTOS CP400 KEXCEPT / ASOWN milestone — one matched kit

Use the `NTOS.hh3` **from this ZIP** with the matching EXEs/DLLs. Backup the known-good firmware. The guest tests run a narrowly prearmed SH4A user-mode fault; if NTOS resets, restore the known-good launcher. Do not use a mix of builds.

Run in order:

1. `KTHREAD.EXE`: **0x4B540010**, PID=257, TID=65537 on first launch; SVC=3 YIELD=1 BADGATE=0.
2. `HELLO.EXE`: **0xC0FFEE42**.
3. `KEXCEPT.EXE`: **0x4B455801**; new per-KTHREAD exception record delivered=1, STATUS_ACCESS_VIOLATION, correct fault address, stored bytes unchanged. Device may also report PROT=1, REC=0, USRRET=1 because legacy REC counts only the earlier one-shot guard.
4. `ASOWN.EXE`: **0x41534F10**; root owner=current PID, VM commit works, committed-page live release denied safely. Run twice across separate launches.
5. `MMGUARD.EXE`: **0x47524401**, PROT=1 REC=1 USRRET=1.
6. `MMREAD.EXE`: **0x52444101**, PROT=1 REC=1 USRRET=1.
7. `MMRDINIT.EXE`: **0x52444931**, PROT=1 REC=1 USRRET=1.
8. `NATIVEVM.EXE`: **0x4E560010**, HINT=5.
9. `VMTEST.EXE`: **0x564D0010**, HINT=8.

If any result differs, report exact hex value, PF/HINT/EV/BAD lines, and whether NTOS returns to the picker. These tests do not enable arbitrary SEH, concurrent processes, a hardware context switch or live decommit. The scheduler context and decommit are host-preflight only, disabled from device calls until safe interrupt-masked UTLB+ITLB verification on the CP400.
