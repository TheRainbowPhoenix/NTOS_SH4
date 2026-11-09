# SuperH DLL boundary ABI candidate (not MSVC-certified)

NTOS runs native **big-endian SH4AL-DSP without an FPU**. A Windows CE
Microsoft SH4 binary normally targets **little-endian** SH4. Converting
a binary to PE32 or matching an API symbol does not change endianness.

## Design

Keep normal GCC calling conventions **inside** NTOS and its DLLs. Only
public export declarations and imported IAT *function pointer types*
may opt into a different ABI. An import must be annotated on the
function pointer type: changing an export declaration alone does not
change code generation at the call site.

GCC's SuperH backend implements the function/struct
`__attribute__((renesas))`. It has **no supported**
`__attribute__((msvc))` for this target. The Renesas convention may
differ from Microsoft's Windows CE SH4 convention (especially the
four-register home area, fifth argument, structure returns, varargs and
software floating point). Do not rename it to MSVC ABI until compared
against independent Microsoft-compiled SH4 fixture assembly.

The `payload/ntabi.h` marker `NT_ABI_CE_CANDIDATE` expands to GCC's
Renesas attribute **only** in `NTOS_SH_RENESAS_ABI_PROBE=1` builds.
In normal builds it expands to nothing. `NT_IMPORT_CE_CANDIDATE` in
`ntimport.h` attaches the attribute to the IAT function pointer.
`NT_CE_CANDIDATE_EXPORT` attaches the attribute to the exported
function declaration. Normal `NT_IMPORT` and the pager stay unchanged.

## Independent experimental build

Run:

```sh
make ce-abi-probe
```

This creates `dist_ce_abi/CHAIN.DLL`, `COREDLL.DLL` and
`ABITEST.EXE`, without changing `dist` or `dist_pager`.
`CHAIN.DLL!chain_abi5` accepts five 32-bit integer parameters and
calls native-GCC `chain_echo()` internally. `ABITEST.EXE` calls the
export through an attributed five-argument IAT function pointer.
Expected return: **0x0AB1C0DE**. A return of 0 means ABI mismatch in
this probe, not a Microsoft ABI verdict. Validate in emulator.

Build CI only checks cross-compilation and PE packaging. A runtime
pass proves the Renesas-attribute caller and callee match **each
other**, not that they match MSVC.

## Requirements before advertising true MSVC compatibility

Compare against actual Microsoft Windows CE SH4 assembly for 0–8
arguments, the mandatory home area, 64-bit integers, small/large
structures by value, hidden structure returns, varargs, callbacks,
callee-preserved registers and stack alignment. Windows CE SH3
materials show r4–r7 and a 16-byte home area, but SH4 compatibility
must be verified directly. Exclude all signatures needing SH4 FPU
registers (absent on CP400) or implement carefully specified software
conversion wrappers. Keep C++ name mangling, vtables and exceptions
behind C-compatible exports.

If any convention differs, use narrow, signature-specific SH assembly
thunks that explicitly copy/translate arguments, rather than applying
`renesas` across unrelated DLLs or inventing an attribute understood
by GCC. The native public export can be a small ABI-specific wrapper
calling an ordinary internal C function.
