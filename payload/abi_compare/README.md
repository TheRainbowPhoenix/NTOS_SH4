# SuperH ABI reference tests

Start with `docs/MSVC_SH4_ABI_RULES.md` for the measured MSVC C contract and
`docs/SH4_ABI_COMPILER_COMPARISON.md` for the MSVC/SHC/GCC differences.

## Sources and scripts

| File | Purpose |
| --- | --- |
| msvc_abi_probe.c | User's six-function probe; original unsupported noinline spelling retained |
| msvc_abi_extended.c | Register pressure, frame pointer, aggregate/64-bit returns, split args, small returns, varargs, FP and convention-keyword fixtures |
| build_msvc_rules.py | Compile MSVC Od/O2, default SH3 and explicit SH4; record exact original failures and compiler/backend hashes |
| check_msvc_rules.py | Execute the six pool-free MSVC integer bodies in BE clemu; check values, result buffer, SP/PR and R8-R14; reject incorrect fifth-argument placement |
| compare.c | Identical vendor/GCC fixtures for argument, return, aggregate, FP and layout comparisons |
| build.py | Produce real MSVC/SHC listings; optionally native and renesas-attribute GCC listings |

Related existing GCC experiment sources are `payload/abi_test.c`, `chain.c`,
`ntabi.h` and `ntimport.h`. The full NTOS repo supplies `make ce-abi-probe`;
this archive is a reference collection, not a standalone replacement NTOS tree.

## Reproduce in the original repo, or the same extracted folder layout

```powershell
python payload/abi_compare/build_msvc_rules.py
& C:/Users/Phoebe/Documents/git/clemu-310c/venv/Scripts/python.exe payload/abi_compare/check_msvc_rules.py
python payload/abi_compare/build.py --gcc
```

Requirements: the supplied MSVC CEPB211 and Renesas SHC 9.4.3 installations,
Python, clemu's Python module for runtime tests, and the Hollyhock compiler
container for optional GCC comparisons. Local compiler paths are configured at
the top of the two build scripts; update them when moving to another machine.
Compiler binaries, SDK headers and container contents are not included.

The standalone checker needs clemu but no pefile or NTOS build. It deliberately
accepts only the pool-free original probe, not arbitrary extended COFF objects.
The extended fixtures and vendor comparison are assembly evidence, not additional
runtime passes. Original noinline compilation failures are expected and retained.

## Saved evidence

* `obj_ce_abi/msvc-rules/`: Od/O2 assembly, COFF objects, compatible generated C,
  build logs, commands/hashes and runtime.json: 24 positive checks + 4 negative controls.
* `obj_ce_abi/compare/`: MSVC assembly/COFF, SHC LE/BE/no-FPU assembly, native and
  renesas-attribute GCC assembly, commands and source/compiler hashes.
* `obj_ce_abi/payload/{abi_test,chain}.elf`: inspected original GCC ABI experiment.
* `dist_ce_abi/`: existing isolated GCC probe executables/DLLs, when present.
* `SHA256SUMS.json` in the archive: hashes of every other bundled file.

## Using this for trampoline work

Keep the saved vendor outputs as independent oracles. Add GCC caller and callee
wrappers to the matching typed fixtures, and test both call directions plus
callbacks. Verify SP/home slots, return buffers, native fixed state (GBR),
nonvolatile registers and wrong-convention negative controls. A GCC-to-GCC pass
is not vendor ABI compatibility. Treat FP/C++/SEH cases separately from the proven
fixed-word C boundary; see the notes for the exact scope and remaining gaps.
