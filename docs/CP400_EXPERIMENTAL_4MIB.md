# Experimental fx-CP400 4 MiB PE run — opt-in, not a safe release

This is a practical **real-device** test plan for experienced NTOS
developers who have a recovery path and accept the risk of an unstable
SH7305 VBR/UTLB handoff. It is not a claim that the experimental pager
has been hardware-proven. The prior conservative Ob/Ex smoke tests
validate a different, no-VBR execution path.

## Artifact and prerequisites

GitHub Actions workflow `CP400 · experimental 4 MiB execution probe`
uploads `NTOS-CP400-UNVERIFIED-VBR-4MiB-EXPERIMENTAL`.

Its `dist_4m_hardware/NTOS.hh3` is built with all of
`NTOS_ENABLE_TLB_MISS_PAGER=1`,
`NTOS_ENABLE_IRQ_OWNERSHIP=1` and
`NTOS_STREAMED_IMAGE_LOADING=1`. It includes the 14-pixel font
fix and the Ob/Ex typed-handle/flash-gate changes.

**Do not mix firmware builds or import DLLs from different commits.**
Keep your known-good `NTOS.hh3` separate and preserve a backup of
the CP400's important files. Do not run on the only device that holds
valuable data, and make sure you know how to recover the normal add-in.
Disable unrelated experimental modules. If the OS reports a filesystem
close failure, stop; do not retry opening or closing files.

Copy the experimental `NTOS.hh3` into the normal add-in location and
the following **matching** EXEs and DLLs into `\\fls0\\`
(the loader searches `\\fls0\\Windows\\System32\\`
first for DLLs, then has a root fallback).
Only one matching `NTOS.hh3` should be installed.

## Five-step read-only test

1. Run `HELLO.EXE` and `OBTEST.EXE`, confirming exit to the
   launcher and `OBTEST.EXE -> 0x0B0B0010`. The font should display
   E/F/B correctly. These validate the experimental firmware's small
   mapped-page bootstrap before stressing its pager.
2. Run `PAGERTEST.EXE` with `PAGERSTRESS.DLL`. It uses
   `page_hint` on 80 BSS pages, writes only into its **RAM image**,
   and deliberately evicts TLB translations. Expected
   `Payload returned 0xE0000CA8`, `BAD=0`, `HINT>=160`,
   `EV>0`.
3. Only if step 2 returned without freezing or MMU error, run
   `LARGE4M.EXE` alone. This test reads 1,024 initialized 4 KiB
   pages and **does not write to flash**. Expected
   `Payload returned 0x4B029400`, `BAD=0`,
   `HINT>=1024` and `EV>0`. File size
   **4,195,840 bytes**; initialized image payload
   **4,194,304 bytes**. Loaded mapped size **4,198,912 bytes**.
   No duplicate 4 MiB full-file staging should be needed.
4. Run `CHAINTEST.EXE` with `CHAIN.DLL`, then
   `HELLOCORE.EXE` with `COREDLL.DLL`.
   Confirm clean close and usable launcher on each return.
5. Optionally run `STRESSEXEC.EXE` after installing all matching
   `ST01.DLL` through `ST12.DLL`, then run `OBTEST.EXE`
   again to verify no leaked process-owned resources.

The most important numbers are the payload return code and
`PF`, `HINT`, `EV`, `BAD` counters displayed by the
launcher **after the app returns**. A successful explicit-hint test
does not prove unhinted genuine VBR TLB-miss delivery. Do **not**
deploy `FAULTTEST.EXE` as part of this kit.

## If anything fails

Do **not** repeatedly rerun the failing 4 MiB or pager payload.
Record the last displayed screen, the exact test binary and firmware
version, and whether the calculator returned to the picker.
A freeze or immediate reset may prevent diagnostic output. Restore the
known-good conservative `NTOS.hh3` before doing any unrelated work.

The firmware makes every effort to close each PE read-only SDK
descriptor before switching to guest MMU/VBR ownership. On an
ambiguous SDK close failure, its FS gate remains poisoned and does not
attempt a dangerous second close or reopen; this is *fail-stop*, not
a guarantee that the SDK did close the descriptor.

## What CI has verified — and what it has not

CI cross-compiles the real SH4BE HH3 and all PE binaries, tests Ob/Ex
and the mocked flash SDK gate, and compares **all 1,026 mapped pages**
of `LARGE4M.EXE` for streamed versus buffered materialization.
CI does **not** execute the PE on SH7305 silicon or emulate the
host device's VBR/TLB fault behavior. A hardware run is the experiment.
