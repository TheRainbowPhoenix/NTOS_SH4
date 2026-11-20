# CP400 real-device controlled SH4A protection fault recovery — development

This branch is an **unverified, high-risk real-device candidate**, not
a passing runtime result. The reported fx-CP400 observations were:

- All ordinary non-destructive CP400 MM, virtual-memory, DLL, process,
  Ob and HELLO regressions **passed**.
- `MMGUARD.EXE`: host OS reported **PROTECT (W)**,
  `TEA=00401000`, `SPC=0040026E`, then calculator rebooted.
- `MMREAD.EXE`: `0xBAD94006`, `PF=0 HINT=1 EV=0 BAD=0`.
- `MMRDINIT.EXE`: `0xBAD96005`, `PF=0 HINT=1 EV=0 BAD=0`.

## Architectural diagnosis and fix

NTOS runs Windows CE PE entry points by **direct privileged function
call**. It does not yet have independent user-mode threads or a
supervisor-call gate for every API.

The old guarded exception engine unconditionally rejected an exception
whose saved SSR.MD was 1 (privileged). Thus an *actually raised*
write-protection exception on a RO page was forwarded to the old OS
handler instead of reaching the pre-armed NTOS recovery label.

The guard now records the exact expected SR.MD at arming and accepts
a privileged write fault **only** when the exact TEA byte address,
EXPEVT=0x0C0, executable original SPC and authorized recovery PC all
match. Unexpected events still chain to the old OS VBR. This is not
general host exception swallowing.

The old read canaries had a more fundamental problem: SH4A PTEL.PR=01
means **supervisor read/write**, user inaccessible. Its read is lawful
when the PE main runs privileged; changing PTEL cannot fix that. The
new `payload/user_read_probe.h` implements a tiny SH4A .text stub:

1. In privileged P0, preserve the caller's PR and SP (no software
   context switch), read SR, clear MD/RB/BL, set IMASK=15;
2. After an LDC SR + NOP, execute exactly ONE byte load from the
   explicitly registered PR=01 page in **user mode**. No SDK calls,
   nested syscall, P1 access, stack push/pop or allocation;
3. On a genuine user read protection violation, NTOS VBR+0x100 sees
   EXPEVT=0x0A0, checks exact TEA/SPC and redirects to the known
   `TRAPA #0x7B` instruction, preserving saved user SSR;
4. SH4A TRAPA generates EXPEVT=0x160, TRA=0x1EC and saved SPC
   pointing to the instruction AFTER TRAPA. The handler accepts
   this ONLY while the read-probe is armed, from user mode, at
   the exact expected return PC. It restores the original
   privileged SR into SSR and RTE returns to the immediately
   following RTS, then the caller checks real `REC` counters.
5. If the read is allowed unexpectedly, the same TRAPA returns to
   the privileged caller, which reports its original BAD code.
   It must never fabricate a positive 0x0A0 or force success.

This pattern is inspired by the strict save/restore/return discipline
in [gint-exword's exception handler](https://github.com/TheRainbowPhoenix/gint-exword/blob/master/src/kernel/exch.s),
not a direct import of code. See Renesas SH4A manuals for
`SR.MD`, `EXPEVT` and `TRAPA`'s saved `SPC=PC+2`.

## Device packages / required preflight

The ordinary **physical candidate** leaves the VBR+0x100 guard
DISABLED and does not package deliberate faults.

The separate opt-in **REBOOT-RISK** artifact uses
`NTOS_ENABLE_GUEST_PROTECT_DISPATCH=1`,
`NTOS_VERIFY_UTLB_SLOTS=1`,
`NTOS_SH4A_TLB_SYNC_BARRIER=1` and strict existing flash
single-owner gating. Its `NTOS.hh3` and EXEs must be from the SAME
artifact. Keep a known-good HH3 and backup flash contents. The device
may still reset from a failed exception return.

Before running a faulting executable on the real calculator, run
`PROTSTATE.EXE` from that package. It makes no invalid accesses:
- **`0x50520001`**: the current P0 foreground guest is privileged
  with RB=BL=0, as expected for this two-mode probe.
- `0x50520002`: already executing as user; the user-mode transition
  is NOT compatible; do NOT run the read canaries.
- `0xBAD95001`: RB/BL state unexpected; do NOT run any protection
  canary.

Then run `HELLO.EXE`, `MMROTEST.EXE` (expected `0x4D520010`),
and `VMTEST.EXE` (expected `0x564D0010`). They must pass, with
`BAD=0` and clean return to launcher.

On the risk-labelled package only, **one canary per reboot/clean
launch**, stop at any reset or failed result:

| EXE | Expected payload code | Expected NTOS counters |
|---|---|---|
| `MMGUARD.EXE` | `0x47524401` | `PROT=1 REC=1 UNEXP=0 USRRET=0 BAD=0` |
| `MMREAD.EXE` | `0x52444101` | `PROT=1 REC=1 UNEXP=0 USRRET=1 BAD=0` |
| `MMRDINIT.EXE` | `0x52444931` | `PROT=1 REC=1 UNEXP=0 USRRET=1 BAD=0` |

For the read tests, `HINT=1`, `PF=0`, `EV=0` are normal.
`PROT=1` is the **real hardware EXPEVT protection event count**,
not a fabricated software probe. The TRAPA is counted separately
in `USRRET`.

A host OS PROTECT screen, any reboot, unexpected general event,
missing USRRET, `BAD93xxx/BAD94xxx/BAD96xxx`, or
`PROT != REC` is failure. Do not continue a test sequence after
a failure. The bare-metal real hardware test is still needed:
GitHub Actions only assembles, links and host-tests CPU policies.

## Safety and limits

- SR transition and TRAPA operate on a prevalidated P0 executable
  stub and no other guest code; the dedicated P1 VBR stack keeps all
  SDK and filesystem calls OUT of exception context.
- The brief user-mode window keeps r15 pointing to an unchanged
  privileged P1 stack, which the hand-written stub **never touches**
  while user mode. Only the CPU saves its value to SGR, as per the
  SH4A architecture. A nested NMI or uncontrolled exception is still
  unsafe; there is no general preemptive process isolation.
- Nonmatching exceptions are forwarded to the host OS, so **some
  failures can reboot**. This is an intentional fail-closed property.
- No live decommit/COW/WinCE user/kernel syscall ABI yet.
- If SDK `File_Close` fails or is ambiguous, never retry it; the
  filesystem gate stays poisoned and NTOS must stop. None of the
  VBR handlers calls the SDK filesystem.
