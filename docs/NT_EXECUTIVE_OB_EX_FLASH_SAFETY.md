# Minimal NT Executive (Ob/Ex) and 4 MiB PE: safety contract

**Development branch, not approved for physical CP400 hardware.**
This branch is stacked on the streamed PE loader and still uses an
experimental RAM-backed SH7305 VBR/TLB miss handler for oversized images.

## What is implemented

- `src/nt/object.hpp`: reference-counted typed objects with a bounded
  64-entry per-launch handle table. Handles are **positive opaque 32-bit
  cookies** monotonically allocated from 0x40000001, never recycled across
  launches until exhaustion (which fails closed). They are not raw P1
  pointers, handle-table indices or filesystem descriptors.
- `src/nt/event.hpp`: manual-reset and auto-reset event objects and
  non-blocking waits. Nonzero-timeout waits on unsignaled events explicitly
  return `STATUS_NOT_SUPPORTED` until scheduling exists.
- `src/nt/pool.hpp`: bounded allocation tracking using an injected
  allocator, tagged ownership, live/peak byte counters, invalid/double-free
  rejection and release-on-exit. The Ex pool is for its *new* API only;
  existing untagged WinCE `LocalAlloc` remains untracked.
- `src/nt/process.hpp`: foreground launch ownership and handle/Ex
  allocation cleanup. **Not separate address spaces.**
- Appended `NtosApi` slots 28–37:
  `create_event(manual,initial)`, `set_event(handle)`,
  `reset_event(handle)`, `wait_event(handle,ms)`,
  `close_object(handle)`, `ex_alloc(bytes,tag)`,
  `ex_free(ptr,tag)`, `ex_pool_peak()`, `ex_pool_live()`,
  `ob_handle_count()`.
- Guest file and finder API calls use the same typed table; the actual
  CP400 SDK raw descriptor is **private to** NTOS. The older COREDLL
  WinCE handle layer also uses monotonic cookies, not reused array indices.

## Flash-safety invariants

1. Only one SDK file or finder descriptor may be owned at a time. The
   existing `ntos_fs::Gate` rejects every second `File_Open` and
   `File_FindFirst` while it holds an owner.
2. Guest handles must match the current generation-free monotonic cookie
   **and** expected type. Stale file handles, double-close and using a
   finder as a file fail *before* issuing a second SDK operation.
3. On a positively successful SDK close, release the raw gate, then
   invalidate the guest handle. This ordering means a guest cannot use
   a logically closed SDK descriptor.
4. When an SDK close returns a negative/ambiguous result, `Gate.poisoned`
   is latched. All future open/read/write/seek/stat/find/close calls
   are rejected; even `fs_recover()` does **not retry** a possibly
   recycled SDK descriptor. NTOS reports a fatal condition and exits.
5. Any leaked guest file descriptor on a normal application return is
   closed exactly once by the gate, **after restoring host MMU/VBR**,
   before the process's Ob/Ex resources are destroyed.
6. No direct flash calls from the page-fault interrupt handler. The
   streaming loader opens and closes all images in host P1 context before
   transferring MMU/VBR control to the guest.
7. Unclear SDK close status cannot establish that flash has physically
   closed the descriptor. This patch can **guarantee fail-stop and
   no retry**, not successful cleanup in a malfunctioning SDK. In that
   state **stop using NTOS** and return to a clean host session;
   do not attempt another NTOS file operation or launch.

## Tests

### Host / CI

The branch-specific GitHub Actions workflow runs:
- `tests/fs_guard_test.cpp` for exclusive ownership and poisoned-close
  no-retry behavior.
- `tests/nt_executive_test.cpp` for Ob type checks, stale tokens,
  event transitions, tagged Ex allocation failure, double-free,
  process teardown and cross-launch token freshness.
- Regular SH4BE cross-compiler build and two rollback builds.
- `make nt-exec-probes` to create the two **opt-in** PE binaries.
- `tools/check_large4m_pe.py` to validate four MiB of real initialized
  data, not BSS, and `tests/pe_stream_test.cpp` to compare pages from
  streaming and full-buffer sources.

### Emulator, sequence

1. Boot a fresh copy of experimental NTOS. Run `OBTEST.EXE`;
   expected return `0x0B0B0010`. Run 10 times, then check no
   object or ExPool resource accumulation.
2. Run `CHAINTEST.EXE`, `STRESSEXEC.EXE`, `CALC.EXE` and
   `STARTHOST.EXE` from the *matching* build; confirm no filesystem
   lock, no error screen and normal return to the launcher.
3. Run `PAGERTEST.EXE` with its DLL first; only if page-hint evictions
   work and `BAD=0`, try `LARGE4M.EXE`. It explicitly hints each
   4KiB page, checks a byte per page, and returns
   **`0x4B029400`** after touching all 1024 pages.
4. Verify diagnostic counters report `HINT>=1024` and `EV>0`
   (the observed exact eviction count depends on other mapped pages).
5. Attempt a repeated launch and exit after a *short read* or a bad
   import to verify clean error handling without extra file handles.
6. A deliberately failed SDK close must be tested **only with a
   mocked SDK in host tests**. Never inject a close error on real flash.

### Real device

**Not until** emulator code/UTLB validation, matched DLLs and host-state
restoration are confirmed. Keep a known-good `NTOS.hh3`. Do NOT run the
4MiB fixture on a CP400 merely because the toolchain compiled it.
The conservative 55-page build cannot execute a 4MiB mapped PE.

## Practical memory limit

The fixture contains **4,194,304 initialized bytes**, so the loader
must hold at least ~4MiB of mapped image plus relocation/import metadata.
It is expressly designed to prove *removal of duplicate on-disk PE
staging* from PR #9. It does NOT prove flash-backed eviction, physical
RAM savings beyond file-stage elimination, or safe hardware TLB misses.

The normal `make all` / `package_device.py` build does not include
`OBTEST.EXE` or `LARGE4M.EXE`. To create tests separately:

```sh
make nt-exec-probes
python3 tools/check_large4m_pe.py dist_ntprobe/LARGE4M.EXE
```
