# Ke per-thread exception records — phase 1

Parent: merged [PR #19](https://github.com/TheRainbowPhoenix/NTOS_SH4/pull/19) (real CP400: KTHREAD 0x4B540010; SVC=3, YIELD=1, BADGATE=0).

Scope of this iteration:
- Each KTHREAD owns a **bounded four-record kernel-side exception chain** and its last NTSTATUS/TEA/SPC/SSR record.
- Registrations are LIFO, have monotonically issued tokens, and pin **exact** P0 guest fault instruction, TEA, SH4A 0x0A0/0x0C0 event, and validated executable PE continuation.
- Dispatch is only for SR.MD=0 user-origin protection faults; wrong address/event/instruction, supervisor faults, stale records and unvalidated continuations **forward to the original host**.
- A successful delivery unwinds registrations above the matching frame and consumes the match. Teardown clears all registrations; a new process starts fresh.
- P1 VBR checks this thread-local chain **before** the existing one-shot MMGUARD/MMREAD/MMRDINIT canary handler; with no registered record its behavior is unchanged.
- No new guest-facing NTOS API slots, no arbitrary guest callback, no TEB/SEH stack unwinding, no process isolation, no context switch, and no changes to MMU/TLB mappings in this phase.

Validation:
1. Host: `g++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 tests/nt_thread_exceptions_test.cpp -o /tmp/ke-exception && /tmp/ke-exception`.
2. Existing `nt_ke_ps_test`, `nt_guest_exception_test`, `nt_virtual_memory_test`, and loader-size gate must remain passing.
3. Physical CP400 regression before enabling guest registration: KTHREAD=0x4B540010, SVC=3 YIELD=1 BADGATE=0; HELLO, MMGUARD, MMREAD, MMRDINIT, NATIVEVM and VMTEST keep their recorded codes.
4. Only after that, introduce a **separately gated** KEXCEPT.EXE using an audited one-instruction user stub to register and exercise a thread-local protection exception.

Next stages (separate hardware-testable PRs): (a) safe user-mode exception registration and TEB-compatible delivery, (b) process address-space ownership with distinct VA->PA frames, (c) explicit cooperative two-thread scheduler with full SH4 banked register/PR/GBR/SR context switch, (d) IRQ-masked, verified UTLB/ITLB invalidation **before** physical frame reuse, then live MEM_DECOMMIT/MEM_RELEASE with rollback and stress tests. Keep the current fail-closed live release until (d) passes on real hardware.
