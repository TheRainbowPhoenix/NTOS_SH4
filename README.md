# NTOS

**"NT loader" for SH4 big-endian COFF EXEs on Hollyhock-3** (fx-CP400).

An HH3 launcher app that loads and runs big-endian SuperH COFF executables —
machine `0x01A8` (SH4 BE), the MIPS-style machine code added to Casio-era
`shasm.exe` by `make_shasm_be.py`, or PE32 images from the MSVC `-B2` backend
route. Think of it as a tiny WinCE-style EXE loader: it finds `*.NTX` files on
`\fls0\`, parses the BE COFF header, copies the image into the HH3 heap,
applies relocations, flushes caches, and jumps into the entry point — handing
the payload an NTOS syscall table the way WinCE hands a loaded EXE its APIs
(and the same trick the ClassPad 330 C1A loader plays with its API window at
VA `0x08100000`).

## How to build

GitHub Actions builds `dist/NTOS.hh3` on every push to `main` and attaches it
to a release for `v*.*.*` tags. Locally:

```sh
# devcontainer (ghcr.io/classpaddev/hollyhock-3) or any env with the
# sh4a_nofpueb-elf toolchain and SDK_DIR pointing at the HH3 sdk/
make -j
```

Copy `dist/NTOS.hh3` to the root of the calculator (USB storage mode) and run
it from the Hollyhock launcher.

## Payload contract

A payload is a raw big-endian COFF EXE renamed to `.NTX`:

* COFF file header, machine **`0x01A8`**, with a PE32 optional header
  (magic `0x010B`) whose `entry` RVA is the code to run.
* Relocations must be present, in one of two flavors:
  * **PE base relocations** (`.reloc`, optional-header data directory #5) —
    what MSVC-linked images carry. NTOS copy-loads the image anywhere in the
    heap and applies `delta = actual_base - preferred_base` to every
    `HIGHLOW` entry. This is the normal case for the MSVC `-B2` route.
  * **COFF object relocations** (per-section tables + symbol table) —
    `DIR32`/`REL32` against section symbols, as emitted by `shasm_be`.
  Images with neither are rejected with a message instead of mis-executed.
* Entry convention (r4–r7, the `.sh4al` scalar ABI):
  `int entry(const NtosApi *api, char **argv, char **envp)`

The `NtosApi` syscall table (field order == table order):

| # | Field | Meaning |
|---|-------|---------|
| 0 | `puts(s)` | write a line to the NTOS console |
| 1 | `printf(x, y, invert, 0, fmt, ...)` | `Debug_Printf` passthrough |
| 2 | `refresh()` | `LCD_Refresh` |
| 3 | `malloc(n)` | HH3 heap (`Mem_Malloc`, 12 MiB at `0x8C280000`) |
| 4 | `free(p)` | `Mem_Free` |
| 5 | `get_key()` | blocking key read, packed `(key1<<8)\|(key2&0xFF)` |

The return value is displayed after the payload exits.

Example payload: `recreate/examples/coff_hello/hello_ntos.c` in the
SHC_SH_Compiler workspace (build through the MSVC `-B2` BE route; keep
relocations — do **not** use `/FIXED` without a `.reloc` directory).

## Design notes

* The HH3 environment runs MMU-off in P1 identity space (the launcher itself
  does raw `ocbwb`/`icbi` after PT_LOAD copies), so NTOS copy-loads and
  relocates instead of using the flash zero-copy UTLB scheme. That scheme is
  documented and implemented for the Casio-OS add-in context in
  `recreate/docs/C1A_MMU_LOADING.md` + `recreate/src/coffloader/` — same
  goal, two OS-appropriate mechanisms.
* Not implemented (yet): imports, TLS, threads, exception unwinding, `.C1A`
  container parsing (payloads are raw COFF, not wrapped).
