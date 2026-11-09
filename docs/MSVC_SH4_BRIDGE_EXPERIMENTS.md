# Two SH4 MSVC ABI bridge experiments (November 2026)

This experiment uses the uploaded measured rules from
`MSVC_SH4_ABI_RULES.md`, recorded using Microsoft shcl
12.00.8281.0 /Qsh4, /Od and /O2, and the accompanying compiler
comparison/archive. These findings are specifically **not** GCC's
`renesas` attribute or Renesas SHC's convention.

## Exactly what is implemented

Both tests implement **fixed-prototype 32-bit integer/pointer words,
five and eight parameters, 32-bit integer returns**.

Microsoft SH4 CE entry: arguments 1-4 R4-R7; arg 5 at entry
SP+16, arg 6 at SP+20, through arg 8 at SP+28. The caller supplies
16 writable home bytes at SP+0..15, even with no overflow.
GCC native SH4 entry: arguments 1-4 R4-R7; arg 5 at SP+0.
The caller cleans up its own argument area. All wrappers save PR
and restore SP; import wrappers also save/restore GBR because
Microsoft's compiled CE code may clobber it.

These are **BE SH4AL-DSP-compatible adapters to the measured
integer-word MSVC convention**, not executable compatibility with
original little-endian SH4 PE files.

## Experiment A: shared runtime trampoline

`make msvc-abi-global` builds:

* `dist_msvc_global/ABIGLOBAL.DLL`
* `dist_msvc_global/ABIGLOBAL.EXE`

The exported entry points have tiny descriptors and branch to
one shared `payload/abi_msvc_runtime.S` entry, which captures
the Microsoft register/stack argument words and calls a native
GCC dispatcher (`payload/abi_msvc_dispatch.c`). The EXE invokes
a single shared `ntos_msvc_call_words(fn,args,n)` assembly routine,
which prepares the MSVC caller home area and stack. The routine
rejects unsupported arities and null targets with `0xBADAB100`.

Shared dispatch is flexible but has additional runtime overhead
and needs an explicit signature count; it is *not* a universal
untyped ABI translation solution.

## Experiment B: generated direct assembly thunks

`make msvc-abi-thunks` builds:

* `dist_msvc_thunks/ABITHUNKS.DLL`
* `dist_msvc_thunks/ABITHUNKS.EXE`

`tools/gen_sh4_msvc_thunks.py` generates two separate SH assembly
translation units (`--role export` and `--role import`). Export
wrappers explicitly move the overflow words from MSVC entry SP+16
to a freshly allocated native-GCC outgoing stack SP+0, then call
the native implementation. Import wrappers do the reverse and
read the destination function pointer from the imported PE IAT slot.
Only signature arities 5 and 8 are in the generator's explicit,
reviewable manifest. No compiler rebuild or global GCC option.

## Emulator test

Install each EXE with its matching DLL, **not both ABI DLLs at
once with an ambiguous import name**, then launch the EXE
in ordinary NTOS or in your known-working `dist_pager` image.

| EXE | Success return | Failure values |
| --- | --- | --- |
| ABIGLOBAL.EXE | `0xAB1C0001` | `0xAB100005`, `0xAB100008` |
| ABITHUNKS.EXE | `0xAB1C0002` | `0xAB100005`, `0xAB100008` |

The first check calls the five-word function with (1,2,3,4,5)
and expects 0x12345055. The second calls the eight-word function
with (1,2,3,4,5,6,7,8) and expects 0x12345114. Both
implementations call ordinary GCC-native C code behind the
MSVC-facing export, and both EXEs use the existing PE import
address table. The generic and generated variants do not share
their wrapper implementation.

GitHub CI **cross-builds** both pairs, and packages them in the
standard artifact. CI cannot confirm the emulator runtime return
codes. None of these experimental DLL/EXE binaries is intended
for an original Windows CE little-endian SH4 operating system.

## Not supported yet

* 64-bit scalar arguments, results in hidden buffers, aggregates
  passed partly in registers and partly on the stack
* Struct returns (including MSVC's hidden result pointer in R4;
  GCC native's out-of-band R2 handling differs)
* Varargs and unprototyped calls
* Floating-point registers, hard-FPU binaries, double alignment
* C++ object/vtable/name-mangling ABI, exceptions, TLS/SEH
* User-to-kernel transitions, arbitrary callbacks and host state

The generator intentionally cannot infer an ABI from a function
name. Extend it using typed signatures, with separate MSVC and
GCC classifiers, and reject unsupported types. For any pointer
to an ABI-crossing callback, generate the reverse adapter too.
Never implement this as a function-pointer cast.

## Evidence

The uploaded `MSVC_SH4_ABI_RULES.md` and
`superh-abi-reference-tests.zip` contain the original
shcl /Qsh4 asm listings, compiler comparison and fixture
execution evidence. The provided runtime test validates the
new NTOS adapters separately and must not be claimed to
re-run the original Microsoft-compiled bodies.
