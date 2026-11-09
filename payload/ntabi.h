/* Optional SuperH boundary ABI experiment.
 *
 * GCC SH supports __attribute__((renesas)) for a FUNCTION or FUNCTION
 * POINTER type. This is NOT an MSVC ABI and should not be marketed as one.
 * Actual MSVC 12 / SHC 9.4.3 comparison confirms distinct conventions.
 * GCC 15.2 renesas also differs from SHC on hidden/64-bit returns.
 * See docs/SH4_ABI_COMPILER_COMPARISON.md for measured home slots,
 * overflow arguments, varargs, structs, returns, and FPU requirements.
 *
 * Nothing changes in standard NTOS payloads unless the isolated
 * NTOS_SH_RENESAS_ABI_PROBE build flag is set.
 */
#ifndef NTOS_PAYLOAD_NTABI_H
#define NTOS_PAYLOAD_NTABI_H

#if defined(NTOS_SH_RENESAS_ABI_PROBE) && NTOS_SH_RENESAS_ABI_PROBE
#if !defined(__GNUC__) || !defined(__sh__)
#error "The Renesas ABI probe requires GCC targeting SuperH"
#endif
#define NT_ABI_CE_CANDIDATE __attribute__((renesas))
#else
#define NT_ABI_CE_CANDIDATE
#endif

/* A declaration using this macro must precede its definition. Keeping
 * the declaration separate makes the exported symbol accessible to
 * ordinary C code while the compiler picks the annotated call ABI.
 */
#define NT_CE_CANDIDATE_EXPORT(ret, name, params) ret name params NT_ABI_CE_CANDIDATE

#endif
