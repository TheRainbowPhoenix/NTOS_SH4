# MSVC Windows CE SH4 C ABI: measured rules for BE thunks

Tested compiler: **Microsoft shcl 12.00.8281.0**, original CEPB211 backend.
This establishes the tested C boundary, not every MSVC release or the entire
C++/exception/runtime ABI. Keep compiler-version-specific contracts.

## Build and runtime evidence

The supplied source is preserved in `payload/abi_compare/msvc_abi_probe.c`.
Both exact requested commands fail with **C2485: noinline is unrecognized**.
The build script records those diagnostics, removes only that attribute in a
generated copy, and adds `/Ob0` after `/Od` or `/O2` to prevent inlining.

Plain shcl selects **SH3**, COFF machine 0x1a2. Explicit `/Qsh4` selects SH4,
0x1a6. We compiled both targets at both optimization levels, and also compiled
separate extended C fixtures at `/Qsh4 /Od` and `/Qsh4 /O2`.

The six original integer functions execute successfully in clemu in all four
builds: **24 positive calls**, checking results, hidden struct result contents,
SP/PR restoration and R8-R14 sentinel preservation. The harness byte-swaps only
the original pool-free instruction halfwords; it does not alter the ABI or
function bodies. This special-case conversion is NOT a generic COFF converter.
An additional negative control places argument five incorrectly at SP+0.

## Entry SP, arguments and home space

Let S be R15 at callee entry, BEFORE register saves/local allocation.

| Word slot | Incoming location | Caller-provided home/storage |
| --- | --- | --- |
| 0 | R4 | S+0 |
| 1 | R5 | S+4 |
| 2 | R6 | S+8 |
| 3 | R7 | S+12 |
| 4 | Memory | S+16 |
| 5 | Memory | S+20 |
| 6 | Memory | S+24 |
| 7 | Memory | S+28 |

Reserve at least **16 writable home bytes**, even with fewer arguments. The
caller need not initialize register homes. The callee may overwrite them:
`/Od abi_1` immediately stores R4 at `@sp`. Never keep caller state there across
a call. Stack grows down; stack/frame alignment is 4 bytes. The caller cleans
outgoing arguments, and the callee restores SP exactly. Do not assume a red zone.
For N fixed U32 words, outgoing storage is at least `4*max(N,4)`; count hidden
arguments too. FP and aggregate classification still requires type information.

### Your fifth-argument question, resolved

At `/Od`, abi_5 has no local allocation and reads e directly at `@(16,sp)`.
At `/O2`:

```asm
mov.l r8,@-sp       ; SP = S-4
mov.l r4,r8
mov.l @(20,sp),r4   ; S-4+20 = S+16
```

abi_8 pushes four registers, then reads arguments 5..8 at current-SP offsets
32,36,40,44: entry offsets 16,20,24,28 again.

Optimized abi_caller saves PR and reserves 20 bytes. Its store of argument 5 to
`@(16,sp)` is in the **BSR delay slot**, so it executes before the callee begins.
Optimization changes spills, frame sizes, registers and delay-slot scheduling;
it does not change any of these measured ABI mappings.

## Return values and the hidden first argument

Your 12-byte ABI_STRUCT return has this effective interface:

```
ABI_STRUCT *abi_struct(ABI_STRUCT *out, U32 x, U32 y)
                       R4             R5     R6
R0 = out; out[0]=x; out[1]=y; out[2]=x^y
```

The result pointer consumes an ordinary argument word and shifts explicit
arguments. Both optimization levels and machine-code execution confirm it.
This differs from native GCC's out-of-band R2 aggregate-result pointer.

Additional compiler fixtures establish:

| Result | Measured MSVC mechanism |
| --- | --- |
| U32/pointer | R0 |
| signed char/short scalar | Sign-extended R0 |
| unsigned char/short scalar | Zero-extended R0 |
| Plain C structs of 1, 3, 4 bytes | Packed in R0; only object bytes are meaningful |
| Plain C structs of 8, 12 bytes | Hidden first pointer R4; write result; return that pointer in R0 |
| unsigned __int64 | Hidden first pointer R4; write 8 bytes; return pointer R0 |
| float | FR0 |
| double | FR0/FR1, not the integer hidden-buffer rule |

The tested LE 3-byte struct packs byte 0 into R0 bits 0..7, byte 1 into bits 8..15,
byte 2 into bits 16..23. High/padding bits of small aggregates are not a portable
integer value. A BE thunk must translate packed bytes deliberately. These cases
do not certify all nontrivial C++ aggregate return classifications.

## By-value structs and 64-bit arguments

Non-scalar words follow object layout through the integer argument stream and
can split across registers and stack. Integer U64 does not force an 8-byte slot
alignment gap. Examples below are original MSVC **little-endian** word ordering.
U is U32, U64 is unsigned __int64, S3 contains three U fields.

| Signature | Locations |
| --- | --- |
| U f(U p,S3 s,U q) | p:R4; s:R5/R6/R7; q:S+16 |
| U f(U a,U b,U c,S3 s,U z) | a,b,c:R4/R5/R6; s:R7,S+16,S+20; z:S+24 |
| U f(U a,U64 b,U c) | a:R4; b low:R5/high:R6; c:R7 |
| U f(U a,U b,U c,U64 d) | a,b,c:R4/R5/R6; d low:R7/high:S+16 |
| Two-float struct, then U | Struct bits:R4/R5; U:R6; no homogeneous-FP aggregate shortcut |

A BE GCC register pair cannot be copied blindly into the original LE word order.
Memory structure packing/alignment and endian conversion are separate obligations.
Default memory alignment can differ from argument-word alignment.

## Preserved registers

| State | Contract and evidence |
| --- | --- |
| R0-R3 | Volatile scratch; R0 also returns scalars |
| R4-R7 | Volatile arguments |
| R8-R13 | Callee-saved; extended pressure fixture saves/restores all six |
| R14 | Callee-saved; dynamic-allocation fixture saves, uses as frame pointer, and restores it |
| R15 | Restore entry SP exactly |
| PR | Keep the incoming return link across nested calls; save before JSR/BSR |
| FR0-FR11 | Volatile FP arguments/results/scratch |
| FR12-FR15 | Callee-saved; extended FP fixture saves/restores all four |

Microsoft's SH4 register/frame documentation corroborates preservation and 4-byte
stack alignment. “Renesas SH-4” in these Microsoft docs names the CPU, NOT the SHC
compiler ABI. [Microsoft Smart Device Development](https://download.microsoft.com/download/1/6/d/16d24ada-5317-4de1-b2b2-890b51813d6e/VS2005_DeviceDev_en-us.pdf).

Microsoft describes GBR, MACH, MACL and status flags as scratch in its CE SHx
convention. An earlier compiled integer fixture also repurposes GBR without
restoring it. If native GCC/NTOS treats GBR as fixed state, the bridge must save
and restore it around CE code. [Microsoft SHx register overview](https://devblogs.microsoft.com/oldnewthing/20190805-00/?p=102749).

A nested call overwrites PR; do not treat it as an extra safe scratch register.
Delay-slot restores are part of the return and must execute. The sentinel tests
prove preservation for the executed bodies, not exhaustive special-register rules.

## Varargs and FP: additional assembly observations

These extended cases were compiled at both levels but were NOT executed by the
integer harness. They require distinct thunk signature classes.

* Variadic function calls use the integer word stream, including a named fixed
  float in the tested `f(float,...)` call. Float ellipsis arguments promote to
  double. The callee spills R4-R7 into home slots for contiguous va_list access.
* Unprototyped calls promote floats to double and duplicate FP arguments in
  integer/stack locations and FP registers. Do not discard prototype information.
* Fixed-prototype FP scalars use FR4-FR11 while integer word positions also advance.
  `f(U,float,U,float,U)` uses R4,FR4,R6,FR5,S+16.
* In `f(U,U,U,U,float)`, the float is FR4 even though its home is S+16.
* Eight floats use FR4-FR11, with 32 outgoing bytes reserved. In a ten-float call,
  the last two floats are S+32 and S+36.
* Doubles use aligned FP pairs; later singles can fill holes. The measured
  `f(float,double,float)` uses FR4,FR6/FR7,FR5. `f(U,double,U)` uses R4,FR4/FR5,R7.
* For the tested double return, FR0 holds the high word, FR1 the low word; LE
  memory stores the low word first. Do not apply integer U64 return rules.

Full FPSCR mode, FPUL and register-bank obligations need explicit handling before
hard-FP thunks. CP400/SH4AL-DSP has no FPU: a wrapper cannot make original FPU
instructions in an MSVC function body executable. Translate/emulate or recompile.

## Calling-convention keywords and scope

The explicit __cdecl and __stdcall five-word fixtures have the same argument,
stack and caller-clean mechanism, with plain underscore-prefixed C names.
Do not import x86 callee-pop or `_name@20` assumptions.

These C probes cannot establish C++ hidden this/result ordering, vtables, mangling,
member pointers, SEH/unwinding, TLS/runtime intrinsics, all packing/bitfield/union
cases, or every FP corner. Those remain separate certification work. Reject
unsupported signatures instead of treating this as one universal MSVC adapter.

## BE GCC thunk requirements

1. Classify the signature, including hidden results, aggregate layout, varargs and
   FP. Keep GCC-native, GCC-renesas and actual SHC mappings distinct.
2. Save incoming PR and required nonvolatile/native fixed state, notably GBR if
   the host relies on it. Allocate a private 4-byte-aligned frame.
3. GCC -> MSVC: provide 16 writable home bytes, and copy fixed overflow words from
   incoming GCC SP+0 to outgoing call SP+16, accounting for your own saved frame.
4. MSVC -> GCC: read fixed overflow words from entry S+16 and put them at a new
   outgoing GCC call SP+0. Never simply shift a live caller SP by 16.
5. Convert native GCC's R2 aggregate-result buffer or R0/R1 U64 result to/from
   MSVC's first-argument result buffer. Rearrange explicit arguments accordingly.
6. Translate packed aggregates, field offsets and LE-format buffers deliberately;
   changing instruction byte order does not define the data ABI of a BE port.
7. Apply reverse wrappers to callbacks/function pointers too. Restore SP/state,
   PR and the delay-slot effect before returning. Pointer casts are not adapters.

## Reproduce

```powershell
python payload/abi_compare/build_msvc_rules.py
& C:/Users/Phoebe/Documents/git/clemu-310c/venv/Scripts/python.exe payload/abi_compare/check_msvc_rules.py
```

`obj_ce_abi/msvc-rules/` contains exact failure logs; default and SH4 Od/O2
assembly/object files; extended Od/O2 listings; `build.json` with commands,
compiler/backend/source hashes; and `runtime.json` with individual checks.
The original probe requests `/FA`; core conclusions do not rely on a decompiler.
