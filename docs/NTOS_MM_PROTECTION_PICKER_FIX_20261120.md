# MM protection + CP400 number-key picker, November 20 2026

## Precisely what the device reported

- `MMROTEST.EXE` -> `0xBAD70002`: intended read-only
  `.rodata` was reported as writable by Mm.
- `MMGUARD.EXE` -> `0xBAD93004`, `PF=0 HINT=1 BAD=0`:
  write guard refused to arm. The RO prerequisite was absent.
- `MMREAD.EXE` -> `0xBAD94006`, `PF=0 HINT=1 EV=0 BAD=0`:
  the forbidden read actually completed. This is **not a pass**.
- `VMTEST.EXE` -> `0x564D0010`, `HINT=8`; both DLL
  lifecycle tests and serial process cleanup passed on real hardware.

## Fixed root cause of MMROTEST and MMGUARD

`tools/elf2ntx.py` had treated every initialized non-executable ELF
section as `IMAGE_SCN_MEM_WRITE`. Thus SH4BE `.rodata` was
mislabelled RW, and `pe_page_access()` correctly considered it RW.
The packer now consults ELF `SHF_WRITE`, retaining writable `.data`,
`.idata`, `.got` and `.bss`, while `.rodata` is read-only.

CI runs `tools/check_protected_pe.py` against the **actual packed**
`MMROTEST.EXE` and `MMGUARD.EXE`. It checks that every 4KiB page
intersecting their `.rodata` does not overlap ANY PE writable
section (MMU permissions are page-granular).

`MMROTEST.EXE` should now return **`0x4D520010`**, HINT=4,
BAD=0. It does no illegal memory access.

## Isolating the second bug, MMREAD

The original `MMREAD` uses a committed RW VM page, hints the RW
translation, then **reprotects** it PR=01/supervisor-only while
live. The physical device still completed the forbidden P0 read.

Renesas SH4A's architecture requires a post-`LDTLB` instruction
synchronization before resuming translated P0 code. The gated SH4A
assembly now executes `ICBI` (any address) + `SYNCO` after LDTLB,
including live PR changes. The *separately gated* emulator profile
also enables P4 UTLB slot readback. Its general exception guard
will refuse to arm a read/write probe unless the actual PR=01/10
entry and its correct VA/PA are present in the expected slot.

New `MMRDINIT.EXE` initializes a private page with PR=01 on the
**first** TLB fill, avoiding live reprotection entirely. It compares
the first-translation behavior with `MMREAD.EXE`.
This is a diagnostic distinction, NOT a fabricated 0x0A0 exception.

`MMGUARD.EXE` uses a correctly protected `.rodata` section and
should arm now. It still requires a real P0 write-protection trap
at VBR+0x100 to return successfully.

## Test order: do not mix HH3 and payload versions

**Device-safe-intent candidate:**
`NTOS-CP400-PRIVATE-VM-DLL-LIFECYCLE-EXPERIMENTAL-NO-MMGUARD`.
Save the previously working HH3 first, use the new matching
`NTOS.hh3`, and copy its matching `MMROTEST.EXE`, `VMTEST.EXE`,
`HELLO.EXE`, `INITPASS.EXE`, `INITGOOD.DLL` etc.

1. HELLO -> `0xC0FFEE42`.
2. MMROTEST -> `0x4D520010`, HINT=4, BAD=0. This is the
   critical verification for the fixed PE section flags.
3. VMTEST -> `0x564D0010`, HINT=8, BAD=0.
4. INITPASS -> `0xD11A0001`, attach=1, detach=1, rejected=0.
5. INITFAIL -> clean `DLL_PROCESS_ATTACH returned FALSE` error,
   followed by a successful INITPASS and OBTEST.

**Emulator-only diagnostic bundle** (distinct HH3):
`NTOS-EMULATOR-ONLY-EXPEVT-0A0-0C0-RECOVERABLE-GUEST-TRAPS`.
Do not execute the intentional bad-access EXEs on the CP400 until
the live emulator has demonstrated one-shot recovery and return
to launcher. The first real 0x100 exception could hang/reset hardware.

| EXE | Deliberate exception | Expected |
|---|---|---|
| `MMGUARD.EXE` | read-only PE write -> 0x0C0 | `0x47524401` and `PROT=1 REC=1 UNEXP=0 BAD=0` |
| `MMREAD.EXE` | live RW to PR=01 then read -> 0x0A0 | `0x52444101` and `PROT=1 REC=1 UNEXP=0 BAD=0` |
| `MMRDINIT.EXE` | first-fill PR=01 then read -> 0x0A0 | `0x52444931` and `PROT=1 REC=1 UNEXP=0 BAD=0` |

Normal HINT counts are one each (the page already exists). Any
`BAD930xx`, `BAD940xx`, `BAD960xx`, missing `PROT=1 REC=1`,
freeze or reset means the actual exception path is **NOT verified**.
`BAD94004` / `BAD94005` and `BAD96004` now commonly
indicate a failed PTEL reinstallation/readback or refused arming;
that is a SAFE failure before the deliberate access.
`BAD94006` or `BAD96005` means hardware still allowed the read:
it must not be reported as success.

The emulator profile compiles `NTOS_VERIFY_UTLB_SLOTS=1` to
read P4 UTLB arrays. That readback mode has *not* been validated
for every emulator/hardware revision; do not assume it is safe on
physical CP400. CI verifies compilation/host policies, not real P4
hardware behavior.

## Picker

Physical HH3 number keys are read through the two `getKey()`
bitfields (no text keyboard required):
- Selected EXE is marked with **`> `** on a 29-row viewport.
- List indices are **1-based**, correctly formatted 1..64.
- Press 2 -> selects item 2; release, press 2 again -> 22.
- Backspace -> removes final digit and reselects item 2;
  press 1 -> selects item 21.
- Out-of-range typed numbers stay visible but never select an
  invalid file. 0 alone does not select index 0.
- Up/Down/Add clears the typed sequence and moves one item.
- EXE launches, CLEAR/LEFT exits NTOS. A held key is not repeated;
  each new digit requires a key release before the next matching key.

The picker performs **no SDK file operations while navigating**.
All existing single-owner SDK file-close fail-stop semantics remain.

## Limits

This is not general Windows Structured Exception Handling:
one-shot guard recovery, not an arbitrary guest fault chain,
exception records or SEH stack unwinding. The VM arena still
rejects releasing committed live pages until physical TLB
invalidation and alias ownership are proven safe.
The corrected PE flags and new barriers are implemented and CI
tested, but a working 0x0A0/0x0C0 handler on the actual CP400
requires runtime evidence.
