# Typed Microsoft SH4 ABI thunks — experimental validation path

**Target:** fx-CP400, SH4AL-DSP, big-endian, no FPU.
**Reference:** user-supplied `MSVC_SH4_ABI_RULES.md` (measured Microsoft
`shcl 12.00.8281.0 /Qsh4`) and `superh-abi-reference-tests.zip`.
This implementation is **not** GCC `__attribute__((renesas))`.
Those vendor ABIs are measurably different.

## Architecture and why this can scale to future DLLs

`tools/gen_sh4_msvc_typed.py` consumes explicit JSON records
`{"name":"symbol","ret":"type","args":["type",...]}`.
For each DLL it generates:

* `abi_export.S`: MSVC SH4 entry symbols, preserve PR/GBR/R8, copy
  R4–R7 and incoming overflow words at entry SP+16 into a private
  eight-word buffer, then call the native-GCC C body;
* `abi_export_body.c`: typed native C marshaling and native function
  call. GCC itself handles its own return ABI (including R2 for
  struct results) rather than unverified inline assembly;
* `abi_import.c`: native-GCC function wrappers and real PE
  `__imp_*` IAT slots; marshal scalar/aggregate arguments into the
  Microsoft word stream;
* `abi_call.S`: native GCC to MSVC outbound call; reserves 16 writable
  home bytes and up to four overflow words outside saved PR/GBR;
* `abi_typed.h`: strict types and native declaration prototypes.

A manifest is necessary: a PE export name does not reveal the function
signature or whether a callback uses the same ABI. The generator
rejects unknown types, duplicate names and >8 argument words.
**It must NOT be automatically applied to every COREDLL export until
its precise signature and unsupported cases have been classified.**
This is a staged path toward default wrappers for all DLLs, not
a silent global ABI change.

## Supported, type-checked cases in the first generator

* 0, 1, 5 and 8 fixed 32-bit scalar/pointer arguments;
* 16-bit and 8-bit signed/unsigned arguments and return extension;
* 1-, 3-, and 4-byte plain small C aggregates packed in R0,
  in logical Microsoft low-byte-first return-word order;
* 12-byte three-word struct arguments passed through registers
  and across the R7/stack boundary;
* 64-bit unsigned arguments packed low-word then high-word in the
  measured Microsoft word stream, including a split R7/stack case;
* 12-byte struct return and 64-bit integer return via Microsoft
  hidden first result pointer in R4; return pointer also in R0;
* BE GCC internal large-struct return automatically handled by C
  adapter, not misclassified as MSVC or Renesas.

The generated import gate always reads an initialized eight-word
array and allocates 32 bytes of outgoing stack (including the
16-byte writable Microsoft home area). The generated export entry
deliberately spills incoming register arguments into the *caller*
home area so inadequate caller stack space is more likely to be
detected by an emulator.

### Explicitly unsupported / rejected

Floating-point arguments and returns, double-bearing structures
or alignment (MSVC default double field alignment is 8, SHC 4),
variadic/unprototyped calls, arbitrary callback/function pointers,
unclassified unions/bitfields, nested aggregates, C++ object ABI,
SEH/exceptions, TLS, and over-eight-word calls. An SH4AL-DSP
cannot execute original MSVC SH4 FPU instructions regardless of
calling convention. The translated ABI word stream is not a
converter for **little-endian** Windows CE instruction/data images.

On real applications, `void *` describes pointer transport only;
pointed-to WinCE structures and buffer encoding must be handled
by their own explicit API contract.

## Build and emulator tests

```sh
make msvc-abi-typed
make msvc-abi-coredll
```

| Test | Install files together | Expected EXE result |
| --- | --- | --- |
| 18-case typed edge suite | `dist_msvc_typed/ABITYPED.EXE` + `ABITYPED.DLL` | `0xAB1C0421` |
| Real four-export COREDLL subset | `dist_msvc_coredll/COREDLLABI.EXE` + `COREDLL.DLL` | `0xAB1C0500` |

**Use separate storage directories or swap one pair at a time.**
The experimental COREDLL only exports four real services plus a five-argument ABI marker and must
never replace the normal production `dist/COREDLL.DLL` for
other apps. The COREDLLABI fixture calls actual NTOS console,
malloc and free services, checks writable allocated memory,
and does not invoke blocking `getkey`. Its coredll_abi5 marker checks the fifth stack argument and distinguishes a correctly wrapped COREDLL from a plain GCC-only DLL.

`ABITYPED.EXE` returns `0xAB1E0001` through `0xAB1E0012`
to identify the first failing of eighteen signatures, in the
manifest's declared order. The success code above only applies
if every check passes.

CI compiles both PE pairs, checks their PE export/import directories,
runs the typed C marshaling oracle under the host compiler, and
checks that unsupported signatures fail generation.
**CI cannot execute the SH4 instructions or verify their emulator
calling convention.** Neither of these new runtime tests has been
confirmed in the emulator yet.

## Existing verified baseline

The user previously tested `ABIGLOBAL.EXE` and
`ABITHUNKS.EXE` successfully, returning `0xAB1C0001`
and `0xAB1C0002`. Those five/eight-word results validate the
older integer-only bridges, not the new struct/U64 typed adapters.

## Next steps before making it the default everywhere

1. Obtain and record emulator returns for both new fixtures.
2. Run a register-sentinel test across R8–R14, GBR, PR and SP;
   repeat calls to detect stack corruption.
3. Add verified typed callback trampolines and explicit varargs
   adapters rather than pretending generic pointer calls suffice.
4. Add per-DLL manifests for known C exports; reject unclassified
   symbols at the release gate. Avoid exposing `coredll_stub`
   as an arbitrary typed MSVC API.
5. Develop a portable fixed-ABI interface for structs containing
   pointers/doubles and WinCE structure packing. FPU signatures
   need a software-only port, not an ordinary SH4 thunk.

Original generic trampoline experiments are retained unchanged for
comparison. Generated, signature-aware thunks are the primary
migration approach because they expose where type-specific
marshaling is required.
