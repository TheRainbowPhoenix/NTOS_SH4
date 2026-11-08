# NTOS dynamic PE module graph and demand-paging preparation

## Built and running by default

The HH3 stage0 mapped loader now discovers named DLLs recursively from
each PE's import directory. It does not hardcode COREDLL or XAMLUI:
it starts with the EXE, adds each new imported DLL from `\\fls0\\`, then
scans those DLLs for further imports. The discovered module registry is
shared, so repeated imports do not reload DLLs and cycles terminate.

The loader uses the existing virtual-address allocator and SH4BE
relocation engine to assign all module bases *before* resolving any IAT
slots. It then snaps imports against the proper named export library,
including ordinal imports. COREDLL and XAMLUI still work normally.

This is bounded rather than unlimited: maximum 10 nodes, with 55 resident
4 KiB UTLB pages **across all nodes** (plus four API-window mappings).
Untrusted DLL paths are restricted to simple filenames under `\\fls0\\`,
and corrupt dependencies, non-SH4BE binaries, unterminated PE import
tables, and exhausted mapping budgets return clear errors.

New real test images:
- `CHAINTEST.EXE` imports only `CHAIN.DLL`.
- `CHAIN.DLL` imports `COREDLL.DLL`.
- All three occupy independently rebased virtual addresses.
- `tools/test_dependency_graph.py` verifies the transitive closure,
  deduplication, missing dependencies and cyclic graphs against the
  actual built PE imports.
- `tools/verify_pe_linkage.py` independently verifies the named exports
  and import thunks.

## On-demand page-source backend (not a live fault handler)

`src/pe_pager.hpp` implements demand **materialization** from an
abstract file reader rather than allocating an entire image:

- Reads only the requested RVA page from the PE headers or file-backed
  sections, with BSS and virtual gaps zero-filled.
- Supports the current NTOS SH4BE `HIGHLOW` page fixups, with PE metadata
  still decoded little-endian and SH4BE pointers stored big-endian.
- Supports file/ROM/memory read callbacks and bounded raw reads.
- Rejects unsupported split fixups and cross-page relocation sites rather
  than treating them as ordinary page-local fixups.
- Models a bounded resident-page cache with pinned/dirty protections and
  LRU replacement; dirty pages cannot be evicted without an eventual
  writeback mechanism.
- `tests/pe_pager_test.cpp` tests a 3.5 MiB virtual image using only
  about 2 KiB of file backing. `tests/pe_page_parity_test.cpp` compares
  reconstructed pages to the actual shipped PE section contents.
- The current eager UTLB loader **already uses this same materializer**,
  but keeps all pages resident. Relocation/imports still occur before AT
  is enabled.

The backend therefore works for explicit page-in operations today;
**it cannot service hardware page faults** until the missing interrupt
integration is implemented. The default runtime rejects an image graph
exceeding the resident UTLB capacity. Do not claim the 3.5 MiB WinCE
samples execute on physical SH4BE.

## Exception ownership boundary

The UBC example supplied by the user configures DBR and UBC registers,
uses banked SH4 general registers, preserves PR and nonbanked registers,
then returns using `RTE` from an exception stub. It is a useful example
of privileged context preservation, but not a suitable TLB miss handler.

NTOS now saves/restores DBR in addition to SR, VBR, GBR and the MMU
ownership check. `src/irq_handoff.hpp` provides a transactional
prepare/install/verify/uninstall protocol, with a rollback if a future
handler install fails.

**Both feature gates remain OFF by default:**
```c
#define NTOS_ENABLE_IRQ_OWNERSHIP 0
#define NTOS_ENABLE_TLB_MISS_PAGER 0
```
There is currently NO installed TLB fault handler; the implementation
does NOT redirect VBR, UBC, or the host interrupt vectors. The future
handler still needs:

1. A P1-resident dedicated fault stack, proven reentrant and safe when
   r0-r7 bank switching changes the active register bank.
2. Fault address and access type capture from the SH7305 MMU registers.
3. Exactly-once UTLB insertion with per-set/four-way replacement.
4. Dirty page writeback and pinning of all IAT, mutable and working data.
5. A safe way to use `File_Read` without calling Hollyhock APIs directly
   inside an exception trampoline with broken host MMU context.
6. Precise preservation/rollback of VBR, DBR, SSR, SPC, SGR, SR, banked
   registers, interrupt masks and native handler state.
7. Emulator *and hardware* tests for fault-under-fault, IRQ reentrancy,
   error paths, program exit and host recovery.

The supplied UBC stub uses static storage for the captured CPU state.
This would be overwritten by a nested exception, so a genuine fault
handler must use a per-exception stack frame instead.

## Compatibility limits

The current WinCE PE loader is NOT yet a fully compatible NK loader:
- No stock SH4LE code execution on big-endian SH4A-DSP.
- No WinCE process API, TLS, DLL attach/detach callbacks or forwarded
  export chains yet.
- No live on-demand fault servicing for DLLs larger than 55 pages.
- No unlimited modules: the current registry and MMU capacity are bounded.
- No actual hardware interrupt takeover or GUI subsystem emulation.

These limitations are intentional: they prevent untested interrupt
changes from overwriting Hollyhock state or producing a soft reboot.
