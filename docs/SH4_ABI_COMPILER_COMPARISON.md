# MSVC, Renesas SHC and GCC SuperH ABI: compiler comparison

Confirmed using the actual local compilers, not inferred from their names:

* Microsoft shcl **12.00.8281.0**, `/Qsh4 /O2 /DSHx`, little endian.
* Renesas SHC **9.04.03.000**, `-cpu=sh4 -optimize=1`, both endian modes.
* Renesas SHC, `-cpu=sh4aldsp -endian=big`, the no-FPU target.
* NTOS GCC **15.2.0**, native and `__attribute__((renesas))`, SH4 no-FPU BE.

Identical C fixtures: `payload/abi_compare/compare.c`. Listings and compiler hashes:
`obj_ce_abi/compare/`. These are assembly comparisons, not a whole-program runtime
certification. The observations below apply to these versions/options and signatures;
compiler switches, CPU/FPU selection and packing are part of the ABI contract.

## Verdict for make ce-abi-probe

**MSVC CE and Renesas SHC are different ABIs. GCC's Renesas attribute is not MSVC
compatibility and is not a complete match for this SHC version either.**

The existing `ABITEST.EXE` sets argument 5 with `mov.l r1,@r15` before calling
CHAIN.DLL. Its attributed callee reads it at its entry SP+0: the disassembly's
`@(20,r15)` is after saving PR (4 bytes) and allocating 16 bytes. MSVC expects
entry SP+16. Therefore the existing five-word probe checks matching GCC-generated
caller/callee conventions, not an MSVC boundary. It also does not exercise the
vendor discrepancies in aggregate and 64-bit returns below.

## Measured differences

SP below always means **callee-entry SP**, before its prologue. Explicit arguments
are numbered from one; hidden result pointers are described separately.

| Signature / property | Microsoft SH4 CE | Renesas SHC 9.4.3 |
| --- | --- | --- |
| Fixed 32-bit scalar args 1–4 | R4–R7 | R4–R7 |
| Fixed scalar args 5–8 | SP+16,+20,+24,+28 | SP+0,+4,+8,+12 |
| Caller register home area | 16 writable bytes at SP+0..15 | No equivalent mandatory area for this case |
| 32-bit integer / pointer return | R0 | R0 |
| `U f(U p, S3 s, U q)`, S3 = three U fields | p:R4; s:R5–R7; q:SP+16 | p:R4; s:SP+0,+4,+8; q:R5 |
| `S3 f(U a,U b,U c)` | Hidden result buffer:R4; a,b,c:R5–R7; result address also copied to R0 in fixture | Hidden buffer:SP+0; a,b,c:R4–R6; R0 not established as result address |
| `S1 f(U a)`, S1 = one U field | a:R4, returns field in R0 | Hidden buffer:SP+0; a:R4; writes the buffer |
| `U f(U a,U64 b,U c)` | a:R4; b:R5/R6; c:R7 | a:R4; b:SP+0,+4; c:R5 |
| `U64 f(const U64 *p)` | Hidden buffer:R4; p:R5 | Hidden buffer:SP+0; p:R4 |
| `f(U fixed,...)`, six U args | First four in R4–R7, remaining at SP+16,+20; callee spills registers into caller home area | All six words on stack at SP+0..20, including the fixed argument |
| Layout `{char a; double b; short c;}` with defaults | b offset 8, c offset 16, sizeof 24 | b offset 4, c offset 12, sizeof 16 |

U is a 32-bit unsigned int; U64 is unsigned 64-bit (`__int64` in MSVC,
`long long` in SHC). Both report sizeof(double)=8 and sizeof(U64)=8 here.
SHC's 64-bit integer is treated differently from GCC's scalar register-pair return.
For BE memory the high word comes first; for LE memory the low word comes first.
Changing byte order does not change the measured home-area/aggregate conventions.

### Floating-point boundary

For `float f(U a,float b,U c,float d,U e)` on **FPU SH4**:

* MSVC: a=R4, b=FR4, c=R6, d=FR5, e=SP+16; return FR0.
* SHC: a=R4, b=FR4, c=R5, d=FR5, e=R6; return FR0.

MSVC leaves positional integer slots for the FP arguments in this example; SHC's
integer allocation proceeds independently. For `double f(U,double,U)` the trailing
U is R7 in MSVC and R5 in SHC. FP packing/order needs its own signature tests.
On SH4AL-DSP, SHC uses integer/stack software-FP paths instead. The MSVC `/Qsh4`
output contains real FPU instructions: an ABI wrapper alone cannot make those
function bodies executable on the CP400's no-FPU CPU. Recompile, translate or
emulate such bodies; a register shim is insufficient.

### GCC renesas is a third measured contract

The real SHC and GCC attribute agree on the simple fixed-word overflow stack area,
stacked S3 argument and all-stack varargs in these fixtures. However:

| Case | Actual SHC | GCC 15.2 renesas attribute, no-FPU BE |
| --- | --- | --- |
| `S3 f(U a,U b,U c)` | buffer SP+0; a,b,c:R4,R5,R6 | buffer SP+0; a,b,c:R5,R6,R7; R4 skipped |
| `S1 f(U a)` | buffer SP+0; a:R4 | buffer SP+0; a:R5 |
| `U64 f(const U64 *p)` | hidden buffer SP+0; p:R4 | p:R4; value returned R0/R1 |
| `U f(U a,U64 b,U c)` | b stacked; c:R5 | b:R5/R6; c:R7 |

GCC native returns S3 using an out-of-band R2 buffer, returns S1 in R0, and returns
U64 in R0/R1. Do not interchange *native GCC*, *GCC renesas* and *vendor SHC*.
A function pointer cast cannot translate any of these differences.

## Concrete evidence: normalize the stack, not the assembly's printed offset

MSVC `take8` pushes R8–R11 (16 bytes), then reads its fifth argument with
`mov.l @(32,sp),r5`. Thus entry SP+16, not entry SP+32.
MSVC `call8` writes 55 (argument five) to `@(16,sp)` before JSR.

SHC `take8` has frame size zero and loads its fifth argument with
`mov.l @r15,r1`. SHC `call8` pushes 88,77,66,55, leaving 55 at SP+0 when it calls.
The indirect `callptr` fixture confirms the same difference for callbacks.

SHC `retstruct` allocates 12 bytes and loads its hidden buffer from `@(12,r15)`:
that is entry SP+0. Its a,b,c are still R4,R5,R6. GCC's attributed `retstruct`
loads the buffer from `@r15` but stores R5,R6,R7 into it. This is an actual
observed vendor/attribute discrepancy, not an endian difference.

## How to translate a boundary

Keep NTOS internals GCC-native. Use explicit, **signature-specific** entry/exit
thunks at CE imports/exports and reverse thunks for function pointers passed to CE.
An IAT target or a returned callback is an ABI boundary even when its symbol name
matches. C++ layouts, mangling, vtables and exceptions need separate work.

For a fixed eight-U function, no aggregate/FP/varargs/result-pointer complications:

```
MSVC caller -> SHC callee:
  preserve R4..R7 and caller PR
  allocate a private outgoing stack frame
  copy incoming [entry SP+16,+20,+24,+28] to [call SP+0,+4,+8,+12]
  call SHC target; restore SP/PR; return R0

SHC caller -> MSVC callee:
  preserve R4..R7 and caller PR
  allocate a private frame with 16 writable home bytes
  copy incoming [entry SP+0,+4,+8,+12] to [call SP+16,+20,+24,+28]
  call MSVC target; restore SP/PR; return R0
```

Do not merely add/subtract 16 from a live caller SP: save/restore state, avoid
source/destination overlap, and account for PR saves and delayed instructions.
Even with <=4 args, an MSVC callee may write its home slots. A Renesas caller must
provide that writable area before entering it.

For the other signatures, build a location map for every field/argument and result:

* By-value structs: copy the fields between MSVC's register stream and SHC's stack
  object; move following scalar parameters to their different registers/slots.
* Hidden returns: move the result-buffer pointer and shift explicit args as the
  **specific destination compiler** requires. Adapt one-word structs and U64 too.
* Varargs: translate the fixed prefix plus the complete promoted argument sequence;
  all-stack SHC is not MSVC register/home-area varargs. Prefer a typed wrapper or
  va_list-formatting implementation with an explicit format/signature descriptor.
* Data: match declared sizes, packing, field offsets and endian conversion before
  handing a shared struct to the other side. `/Zp` cannot fix a calling convention.
* FP: reconcile positional slots and integer/FP register allocation; software-FP
  needs a separate contract. No-FPU hardware cannot execute original FPU bodies.
* Registers: preserve the destination's callee-save state, save PR around nested
  JSRs and restore SP exactly. R8–R14 preservation is visible in fixtures, but this
  suite is not an exhaustive register-clobber/FPSCR/GBR/MAC/SEH certification.

To identify an unknown DLL's convention, compare a known call site and callee:
track SP changes; locate args 5+; inspect register spills at entry; inspect hidden
result pointers, small structs and a varargs call. A <=4-scalar leaf without stack
spills may look identical in both ABIs and cannot classify the DLL reliably.

## Reproduce

From the repo root on this machine:

```powershell
python payload/abi_compare/build.py --gcc
```

Without `--gcc`, only the two supplied vendor compilers run. Their genuine backend
DLLs are used: no replacement `-B2` backend, disassembly recompiler or shim changes
the ABI being measured. The SHC_LIB/SHC_INC environment is scoped to child processes
and points at 9_4_3; the machine-wide ClassPad SDK setting otherwise selects the wrong
backend and produces `C3307 Compiler version mismatch`.

Outputs: `msvc.asm/.obj`, `renesas-le.src`, `renesas-be.src`,
`renesas-nofpu-be.src`, optional `gcc-native.s` and `gcc-renesas.s`, and
`commands.json` containing exact commands and compiler/source hashes.

The dedicated [MSVC Od/O2 probe and BE thunk contract](MSVC_SH4_ABI_RULES.md)
adds executable checks of the original Microsoft-generated integer code.
