# NTOS fx-CP400 P0–P2 probe suite

The 4 MiB explicit-hint probe, PAGERTEST, CHAINTEST, and 12-DLL
STRESSEXEC have now returned their intended checksums on real CP400
hardware. These new probes build **on that specific experimental
streamed VBR/UTLB firmware**, without changing loader/MMU/FS internals.

This is an **opt-in developer diagnostic**, not a general release.
Keep the known-good `NTOS.hh3` backup and important flash contents.
After an ambiguous file-close error or freeze, STOP; never retry the
same SDK descriptor.

## Where the files live

CI workflow: `NTOS P0-P2 executive and MM/IO probes`.

Artifact: `NTOS-CP400-P0-P2-SAFE-INTENT-PROBES`.
It contains one matching `NTOS.hh3` plus
`OBTEST.EXE`, `POOLTEST.EXE`, `EVTEST.EXE`,
`PROCTEST.EXE`, `PROCNEXT.EXE`, `PROCFINAL.EXE`,
`PROCFAIL.EXE`, `MMTEST.EXE`, `IOTEST.EXE`,
`HELLO.EXE`, `COREDLL.DLL`, and the previously
validated `PAGERTEST.EXE` / `PAGERSTRESS.DLL`.

Place the matching EXEs/DLLs under `\\fls0\\`.
Only replace `NTOS.hh3` with the matching candidate if you
want the new build; its launcher logic is unchanged.

## Test order and exact expected returns

| Phase | EXE | Expected result | Meaning |
|---|---|---|---|
| P0 | `OBTEST.EXE` | `0x0B0B0010` | 128 sequential handle reuse cycles, stale and wrong-type file operations rejected, capacity exhaustion, no live handles |
| P1 | `POOLTEST.EXE` | `0x50010200` on a clean run | 4 randomized allocation/free rounds, 1–8192 byte sizes, 512 KiB peak, 128 tracked slots, overflow and tag errors; low 16 bits are reported peak KiB |
| P1 | `EVTEST.EXE` | `0xE7000010` | manual-reset vs auto-reset, zero-timeout polls, unsupported blocking waits, repeated close/reopen |
| P2 | `PROCTEST.EXE` | stages `0x70010001`, `0x70020002`, `0x70030003` | serial handoff to `PROCNEXT.EXE` and `PROCFINAL.EXE` with intentionally leaked event/Ex allocation and read-only file; host recovers descriptor then clears per-launch ownership |
| P2 | `MMTEST.EXE` | `0x4D4D0060`, `HINT=193` (192 successful + 1 deliberately rejected), `EV>0`, `BAD=0` | 96 explicitly hinted RAM pages, forward write and reverse read/validation after UTLB eviction; invalid unmapped VA hint rejected |
| P2 | `IOTEST.EXE` | `0x10F00010` | read-only `HELLO.EXE` header/seek/size, nested open rejection, wrong-type and stale handle rejection, two positively successful closes |

The pool peak is retained by the executive for the lifetime of the
currently loaded NTOS session. If any earlier EX allocation had a peak
above 512 KiB, `POOLTEST.EXE` returns the retained peak in KiB in
the lower 16 bits rather than exactly `0x0200`. A result of
`0x5001xxxx` with `xxxx >= 0x0200` and no internal error is a pass.
It does **not** directly measure physical heap consumption.

### Optional loader-failure check (last)

`PROCFAIL.EXE` deliberately imports `NOFILE.DLL`, which must **not**
be installed. Running it should result in a clean error such as
`PE file unavailable or filesystem busy` **before executing guest
main**, rather than a return code. Then re-run `OBTEST.EXE` and verify
the picker and filesystem still work. This tests missing dependency
cleanup; it does **not** test DLL initialization callbacks.

## What is only tested through host mocks

The CI tests `tests/nt_io_injection_test.cpp` and
`tests/nt_p0p2_host_test.cpp` exercise:
- Short SDK reads and deferred open until a prior descriptor closes
- Busy filesystem denying a nested `File_Open` or `File_FindFirst`
- SDK `File_Close` returning an ambiguous error, permanently poisoning
  the gate and **never** issuing a second close/reopen
- Resource cleanup over 3 simulated process lifecycles, including an
  artificially failed DLL initialization
- 96-page software UTLB replacement and an invalid unmapped-address
  request

**Do not inject these SDK failures on a real CP400.** A failed close
may already have physically closed the raw SDK descriptor. The code
guarantees fail-stop, not successful close after an SDK error.

## Unimplemented properties, deliberately not claimed as PASS

- **True independent processes:** NTOS owns one foreground app graph
  at a time. PROCTEST proves serial teardown, not multiple processes,
  parallel scheduling or protected virtual address spaces.
- **DLL entry-point initialization/rollback:** the loader does not yet
  implement WinCE DLL_PROCESS_ATTACH / DLL_PROCESS_DETACH semantics.
  PROCFAIL tests a missing DLL dependency; a mocked failure tests
  the executive unwind, but neither is a genuine guest DllMain test.
- **P0 page permissions:** the current RAM-backed `Pager::Area`
  stores VA/PA/page count but no RX/RW protection metadata. MMTEST's
  bad hint verifies missing-page rejection, **not** a hardware read-only
  write fault.
- **Spontaneous VBR hardware TLB misses:** MMTEST uses explicit hints.
  `PF=0` is expected, not proof of the real exception trampoline.
  The older unhinted `FAULTTEST.EXE` is intentionally NOT in this
  on-device artifact because a failed VBR handoff can hang/reset.

Next work toward full P2: add permissions to page descriptors and
verify a safe, emulated exception trap before enabling intentional
unhinted hardware faults or protected-write tests.

## Local build

```sh
make nt-safe-probes
make dist_ntprobe/PROCFAIL.EXE
```

Both are opt-in. The default `make all` remains unchanged.
