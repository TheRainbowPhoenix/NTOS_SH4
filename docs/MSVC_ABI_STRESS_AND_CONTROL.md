# MSVC SH4 ABI stress and CONTROL little-endian conversion research

## Validated baseline (emulator)
User reports ABITYPED.EXE = `0xAB1C0421` and COREDLLABI.EXE =
`0xAB1C0500`. These are actual runtime passes of the existing typed
export and real-COREDLL pilots.

## New 2304-call randomized stress
Run `make msvc-abi-stress`, then copy
`dist_msvc_typed/ABISTRESS.EXE` and the **matching**
`dist_msvc_typed/ABITYPED.DLL` into the same NTOS module directory.

Expected return: **`0xAB1C0600`**.

This invokes the 18 supported typed Microsoft-SH4 ABI exports for
128 pseudo-random argument sets, comparing each result to the same
native implementation linked directly into the EXE. Checks cover
register and stack arguments, 64-bit word pairs crossing R7,
three-word structs, small aggregate returns, hidden result pointers,
narrow scalar extension, pointer identity, and zero-argument calls.
On failure, the program returns `0xAB1FSSII`, with SS=signature
index 01..12 (hex, 1..18 decimal) and II=iteration index 00..7F.
No new FPU or unsupported varargs ABI is silently enabled.
A pass establishes those signatures survive repetitive cross-PE
calls, not that all Microsoft C/C++ signatures are supported.

## CONTROL analysis (uploaded examples)

* Original `control.exe`: Microsoft CE SH4LE PE machine **0x01A6**,
  preferred base 0x00010000, entry RVA **0x1254**, five sections
  (.text, .data, .pdata, .rsrc, .cerom).
* Existing `control_sh4BE.exe`: NTOS legacy private **0x01A8**
  (Microsoft assigns that value to SH5), entry RVA **0x8000**,
  extra `.beinit` section, and rewritten PE file offsets.
* Executable .text raw bytes: 8192. Of those, **6216 individual
  bytes** equal the reference's halfword-swapped position, while
  1064 equal the corresponding original byte position. Neither
  statistic proves instruction boundaries; zeros can satisfy
  both comparisons and inline 32-bit literals must remain intact.
* A conservative linear scan sees **14 0xFxxx family halfwords** in
  .text's virtual extent. These are **FPU opcode candidates**, not
  verified executed FPU operations: .text also contains literal
  pools and import data.
* Import descriptors refer to **COREDLL.dll** and **commctrl.dll**,
  with **73** and **6** ordinal imports, respectively, not import
  names. Even with correct translated instructions, proper ordinal
  export tables, the original library contracts and a compatible
  commctrl implementation remain prerequisites.
* No PE base relocation directory was found. Loading at a relocated
  image base needs additional work, or a safe fixed-base allocation.
* PE headers/import descriptors/resource trees are little-endian
  metadata, whereas SH4 CPU-visible data/literal pools require
  their own deliberate transformation. Never byte-swap the entire
  PE file, all .text, or the IAT indiscriminately.

The uploaded `control.exe.entry.S` is a separate tiny entry-stub
reference. It does not supply a disassembly of all of CONTROL's
SH4LE code or a complete relinking map.

## Reusable cautious converter

```sh
python3 tools/sh4le_be_probe.py control.exe --reference control_sh4BE.exe --report control_report.json
```

Optional **partial** instruction conversion:
```sh
python3 tools/sh4le_be_probe.py control.exe \
  --code-ranges reviewed_code_ranges.json \
  --experimental-output CONTROL_PARTIAL_NOT_RUN.exe
```

`reviewed_code_ranges.json` must contain a JSON list of
halfword-aligned, non-overlapping **RVA** pairs
(`[[start_rva,end_rva],...]`). The utility refuses blind conversion
or out-of-range data swaps. It swaps only the reviewed 16-bit
instruction halfwords and writes the private NTOS legacy 0x01A8
machine marker. **It does not make the result runnable**: it does
not disassemble unknown instruction/data boundaries, convert pointer
and literal data, reconstruct relocations, implement ordinal
import libraries, rewrite FPU code, or add a device-safe entry point.
Those must be engineered separately.

For the supplied CONTROL binary, entry RVA 0x1254..0x1288 is an
example range whose halfwords agree with those in the existing
reference BE attempt. The literal pool begins at 0x1288; hence
do not extend that range past 0x1288 by naive linear sweep.
Treat this as a disassembly research fixture, not a deployed image.

## Next missing ABI classifications

No amount of randomized testing of the supported 18 signatures
validates **variadic and unprototyped calls**, FPU registers and
double layout, callbacks whose target ABI differs, arbitrary nested
structs and bitfields, >8 argument words, C++ member-call/exception
ABI, or real DLL ordinal export contracts. The generator must
fail closed for unknown signatures; do not mass-convert every
COREDLL export to an untyped common stub.

A practical next milestone is to use a real SH4LE disassembler
with relocation-aware control-flow and literal-pool mapping, then
re-assemble identified functions for SH4BE, checking each
instruction and data relocation against CONTROL's known BE attempt.
The tool in this commit is an intentionally conservative first stage.
