# Streamed PE file staging (experimental hardware candidate)

This branch changes **file staging only**. It does not add pagefile support,
flash-backed page faults, new MMU ownership, or more than 55 simultaneous
UTLB translations. It preserves the existing resident mapped-image behavior.

## What changed

Previously, each EXE and each imported DLL allocated its entire on-disk file
and then allocated the entire page-aligned mapped image. All source-file
buffers lived until the process finished. Now the mapped loader:

1. Opens one file at a time through the existing `ntos_fs::Gate`.
2. Reads and validates just the PE header using a bounded 1024-byte buffer.
3. Allocates the normal page-aligned mapped image once.
4. Calls `ntos_pager::materialize` on every mapped page, reading individual
   section ranges through `File_Lseek(FILE_SEEK_SET)` and `File_Read`.
5. Closes the SDK file before the next DLL open and **before MMU/VBR handoff**.
6. Relocates and resolves imports exactly as before.

All mapped images still live in RAM at once. Estimated file-staging RAM
removed is approximately the **sum of file sizes** of EXE and DLLs in
the dependency graph, excluding small stack metadata. This is not an
8 MiB memory guarantee: loaded image sizes, allocations, and other runtime
costs still need to be measured.

The fallback copy-load path still stages a complete PE file. If
`MMUCR.AT` is already enabled, it continues to use the old path.
No SDK filesystem calls are made from the fault handler.

## Build variants

- Normal branch: `make -j4 && python3 tools/package_device.py`.
  The streamed mapped-loader flag defaults to **1 on this branch only**.
- Compare with old full-file-staging path:
  `make clean && make -j4 DEFINES="-DNTOS_ENABLE_TLB_MISS_PAGER=1 -DNTOS_ENABLE_IRQ_OWNERSHIP=1 -DNTOS_STREAMED_IMAGE_LOADING=0"`
- Conservative, non-VBR eager loader **with streaming**:
  `make clean && make -j4 DEFINES="-DNTOS_STREAMED_IMAGE_LOADING=1"`
  (only graphs within 55 pages work).
- Conservative previous eager loader:
  `make clean && make -j4 DEFINES="-DNTOS_STREAMED_IMAGE_LOADING=0"`

Always use clean or a separate output tree when changing flags. The Makefile
records `DEFINES` in the object rebuild stamp, but an isolated output folder
is safest for comparing release variants. Do not replace the working
`NTOS.hh3` on real hardware before successful emulator tests.

## Tests

Host/CI:
- `g++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 tests/pe_stream_test.cpp -o /tmp/pe_stream_test && /tmp/pe_stream_test`
- Normal `make -j4` device package and existing PE dependency/relocation tests.
- Also build the flag-off path before comparing the images.

Emulator in this order:
1. `HELLO.EXE` and `HELLOCORE.EXE` (EXE and single import).
2. `CHAINTEST.EXE` (transitive DLL dependencies; expected 0x373F373F).
3. `STRESSEXEC.EXE` (shared preferred bases, 12 DLLs;
   expected 0x1234004E).
4. `CALC.EXE`, `CONTROL.EXE`, and `STARTHOST.EXE` app switching;
   repeat each launch several times.
5. Missing DLL, malformed PE, and a truncated PE: reject cleanly and
   continue to use the file picker. Do not rely on unhinted hardware faults.

Real CP400 **only after the emulator passes**:
- Back up device files and preserve a known-working launcher and `NTOS.hh3`.
- Test first with small, known-good EXE/DLLs from a copied test directory.
- Launch/exit/relaunch at least 10 times; verify CLEAR returns to Hollyhock
  and its filesystem, touch and keyboard still work.
- Capture any load error, file size, last successful module and SDK return
  values. Do not continue testing if a descriptor cannot be closed or the
  original host state is not restored.
- Compare launch time and peak heap consumption with the old build, especially
  for the large multi-DLL phone applications.

## Limits and follow-ups

- `File_Lseek` must return the resulting absolute file offset; this is the
  documented Hollyhock-3 contract. The callback reads short positive reads
  until the requested range is complete and rejects EOF/errors.
- The loader accepts up to eight PE sections and bounded optional headers,
  like the existing `pe::parse` validator; unusual headers that would require
  more than 1 KiB of staging are intentionally rejected.
- The initial EXE header is read and closed for format selection, then
  reopened for materialization. File size and key PE header fields are checked
  again. Do not modify executables while they are being loaded.
- This change reduces memory usage, but performs multiple seeks and flash
  reads. Startup performance and wear need to be observed on the device.
- Allocations are still made for the **entire mapped image**, and physical
  code/data paging out to flash is not yet implemented. Do **not** load from
  `File_Read` directly inside VBR exception context.
