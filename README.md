# NTOS

NT loader for SH4 big-endian PE executables on Hollyhock-3 (fx-CP400).

An HH3 launcher app that loads and runs real Windows-PE-style executables for
the big-endian SuperH CPU (machine `0x01A8` : SH4 BE) with standard PE32
headers (MZ/`PE\x00\x00`, little-endian fields, so `pefile`, `dumpbin` and
Ghidra's PE loader all parse the output directly). Basically a mini
WinCE nk.exe: it finds `*.EXE`/`*.DLL` files on `\fls0\`


## Install on fx-CP400 (one download)

Open [GitHub Actions → Build · copy to calculator](https://github.com/TheRainbowPhoenix/NTOS_SH4/actions/workflows/c-cpp.yml)
and download the **NTOS-CP400** artifact from the latest successful run.

1. Extract the ZIP.
2. Copy **the contents** directly to the calculator's storage root, retaining
   `Apps/` and `Windows/`. Install `NTOS.hh3` as your add-in.
3. On updates, preserve personal `home.dat` and `nav.dat` where present.

This is the **only device artifact**. It contains the single shared
`COREDLL.DLL`, launchable EXEs, runtime DLL dependencies, 24 phone apps,
and `Windows/demo.cpl`. It does **not** contain development tests,
alternative ABI profiles, `dist_pager*`, ELF objects, or build maps.

Build locally with `make -j4 && python3 tools/package_device.py`; copy the
contents of `install/`, not the unfiltered development `dist/` directory.

The device package includes the full Calculator, Edge, File Explorer and
Cortana ports at `Apps/<name>/<name>.exe`, compiled for big-endian SH4 without
an FPU. Their compiled markup is installed at the storage root and shared
runtime DLLs are installed under `Windows/System32`. Swipe down from the
status bar for notifications and quick settings; the system navigation bar
provides Back, Home and Search.

## Windows Edge demo

The same [Build workflow](https://github.com/TheRainbowPhoenix/NTOS_SH4/actions/workflows/c-cpp.yml)
also builds **NTOS-Windows-Edge**, independently of the SuperH device job.
Download and extract that artifact, then double-click `run-edge.bat`.
Keep the executable, five DLLs and `fs/` directory together.

This is a native x86-64 Windows host built from shared UI sources, not an
SH4 emulator. CI uses Zig 0.14.0 to build the executable and DLLs and package
the demo assets; visual regression tests are not required for this job.

CI is intentionally split:
[Build](https://github.com/TheRainbowPhoenix/NTOS_SH4/actions/workflows/c-cpp.yml)
provides the calculator package and the Windows demo,
[Tests](https://github.com/TheRainbowPhoenix/NTOS_SH4/actions/workflows/tests.yml)
checks correctness without uploading test binaries, and
[Experiments](https://github.com/TheRainbowPhoenix/NTOS_SH4/actions/workflows/experiments.yml)
is manual-only for ABI and pager research. CodeQL remains independent.

**Caution:** The default loader uses experimental exception-driven UTLB
paging. It has emulator coverage but is not certified for physical CP400 use.

## Light-theme UI refinements

Start tiles use 24 distinct pastel watercolor colors, with stronger blue
focus outlines and app-list selection highlights. Settings and System >
Display use dark SVF icon ink, readable higher-contrast flat buttons and
a darker pressed state. Touch-dragging the brightness and scale sliders
skips redundant repaints and updates in 5% steps (at most 21 value
changes per full sweep). File Explorer uses dark title text, compact
folder icons, and a visible selected-row highlight.

The host Settings regression under `Tests · no release artifacts` checks
icon tinting, button press/release colors and slider redraw suppression.
Runtime responsiveness still needs confirmation in the CP400 emulator.

## Building your own payloads

The repo builds payloads with the *same GCC toolchain as the launcher* :

1. Compile freestanding: `sh4a_nofpueb-elf-gcc -c -ffreestanding -fno-builtin
   -O2 hello.c`
2. Static-link at the payload base with retained relocations (sections
   0x200-aligned so PE RVAs stay spec-clean):
   `sh4a_nofpueb-elf-ld --emit-relocs -e main -T payload/payload.ld -o
   hello.elf hello.o`
3. Pack: `python3 tools/elf2ntx.py hello.elf -o HELLO.EXE`

`tools/elf2ntx.py` converts the big-endian ELF32 into a standard PE32
image: MZ + `PE`, little-endian headers, WinCE subsystem, PE
`.reloc`. Extra switches:

* `--exports sym1,sym2 --dll-name COREDLL.DLL --dll` : synthesize a PE
  export directory (for COREDLL-style modules)
* `--import-module COREDLL.DLL` : build a PE import directory + IAT from the
  ELF's `__imp_*` symbols (see `payload/ntimport.h`)


## Notes 
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
