# NTOSH – EX-word XD-A / XD-D (SH7305) port

This is a **separate, native .d01 target**, not a copy of the Hollyhock
`NTOS.hh3` binary. It reuses the source for the NTOS portable executive
(`src/nt/process.hpp`, Ke/Ps, Ob handles, Ex events and tagged pool).
Its original WinCE COREDLL loader, P0 process execution, VBR trap handler,
screen driver and CP400 SDK entry point are **not yet supported** here.

## Hardware contract and safety

- EX-word SH7305 is big-endian SH4A/no-FPU, like the ClassPad CPU, but has
  different firmware memory ownership and peripherals.
- The native screen is **528×320**, RGB565. No CP400 320×528 orientation
  constants may be reused in the EX-word HAL.
- The native D01's complete image must be **<= 131072 bytes**.
- This port uses gint-exword's vetted `_exword_entry` and
  `mmu_exword_prepare` to enter a **P1-resident runtime and pin only its
  validated 0x7400xxxx window**, then restore original firmware state on exit.
  **Do not call the CP400 `mmu::disable_at_flush`** from inside the D01.
  MMU translation is still required for EX-word firmware services. Blindly
  writing MMUCR, replacing UTLB slots, or enabling NTOS's CP400 VBR would
  invalidate gint's OS-world switching and likely crash the device.
- Native firmware FS calls must occur inside `gint_world_switch()`.
  The first port **only enumerates** `\\drv0\*` and `\\crd0\*`; it never
  modifies storage, executes a PE, or assumes the system's scratch RAM is ours.
- No automatically allocated ELF arena, `.edl`, or stage-two loader is used.
  The executable's kernel model is initialized, but it does **not** install
  any guest VM mappings or enter a user-mode KTHREAD.

## Build and install

CI: [NTOSH EX-word workflow](../.github/workflows/exword-d01.yml) builds the
native executable with `fxsdk-exword`, `gint-exword` and `elf2d01`, using
the same pinned toolchain branches as Edelweiss.

Resulting artifacts:
- `ntos_sh.d01`: real launchable native EX-word add-in.
- `NTOS.d01`: byte-identical convenience alias.
- `packages/ja/NTOSH/` or `packages/cn/NTOSH/`: installation folder
  including the real D01 and vendor-format metadata; content/app ID **NTOSH**.

Use the appropriate locale's `NTOSH` folder for installation with an EX-word
add-in transfer tool. Install one locale, not both at once. The alias
`NTOS.d01` is a developer copy, not a second installed app.
The binary module ID embedded by `elf2d01` is `ntosh`.
Do not mix Hollyhock `.hh3`, `.EXE` or `.DLL` files into this package.

When the EX-word toolchain is installed, build locally with:
```sh
cmake -S exword -B build-exword \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/fxsdk-exword/lib/cmake/fxsdk/EXWORD.cmake \
  -DCMAKE_PREFIX_PATH=/path/to/gint-exword \
  '-DCMAKE_MODULE_PATH=/path/to/gint-exword/lib/cmake/fxsdk;/path/to/fxsdk-exword/lib/cmake/fxsdk' \
  -DEXWORD_METADATA_SOURCE=/path/to/exword-xd-a8500-TEST
cmake --build build-exword --parallel 2
python3 tools/check_exword_d01.py build-exword
```

The root `make exword EXWORD_TOOLCHAIN=... EXWORD_GINT_PREFIX=...`
is a shortcut. Neither `make all` nor existing Hollyhock CI changes.

## Hardware smoke test

Back up a recoverable EX-word and verify its model/ROM is supported by
gint-exword before running the D01. Start **NTOSH**, check that it draws a
white 528×320 dashboard, displays the Ke/Ps readiness line, allows left/right
selection of `\\drv0` and `\\crd0`, and returns with EXIT. Press EXE
to rescan storage. Errors are displayed as hexadecimal OS status codes.

**Passing CI is not proof of hardware boot or safe universal ROM support.**
The backend's ROM-specific TLB assumptions and framebuffer must be validated
on the actual device before enabling further kernel work.

## Towards NT cmd and SH4 WinCE executables

1. Verify boot and exit, startup MMU lease and both volumes on hardware.
2. Build an EX-word HAL (owned display, keyboard, and filesystem handles),
   with read/write only once OS-world transitions are tested.
3. Add a tiny command interpreter using read-only `dir`, `type`, `ver`
   and `help` semantics without shipping Microsoft's NT5 source verbatim.
4. Only then port the PE loader/COREDLL by providing an owned executable
   RAM arena, validated TLB/ITLB invalidation, and a safe guest VBR/trap path.
   The physical CPU instruction set being the same is **not** sufficient
   to run the existing CP400 EXEs unchanged.
