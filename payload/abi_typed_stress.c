/* MSVC ABI stress: 128 rounds x 18 typed exports.
 * Compare PE-imported thunk calls to linked GCC-native oracle functions.
 * 2304 cross-ABI calls, varied arguments. Expected 0xAB1C0600.
 */
#include "abi_typed.h"
#include <stdint.h>
#define FAIL(k, n) ((int)(0xAB1F0000u | ((uint32_t)(k) << 8) | ((n) & 255u)))
#define CHECK(k, expr)                                                                             \
    do {                                                                                           \
        if (!(expr))                                                                               \
            return FAIL((k), i);                                                                   \
    } while (0)
int main(void)
{
    uint32_t seed = 0x7C91E3A5u;
    for (uint32_t i = 0; i < 128u; ++i) {
        uint32_t a, b, c, d, e, f, g, h;
        NtosAbiS3 s, sr, gr;
        NtosAbiBytes3 t, tr, gt;
        NtosAbiBytes1 b1, g1;
        NtosAbiBytes4 b4, g4;
        uint64_t q, rr;
        uint32_t slot;
        void *ptr;
        seed = seed * 1664525u + 1013904223u;
        a = seed;
        seed = seed * 1664525u + 1013904223u;
        b = seed;
        seed = seed * 1664525u + 1013904223u;
        c = seed;
        seed = seed * 1664525u + 1013904223u;
        d = seed;
        seed = seed * 1664525u + 1013904223u;
        e = seed;
        seed = seed * 1664525u + 1013904223u;
        f = seed;
        seed = seed * 1664525u + 1013904223u;
        g = seed;
        seed = seed * 1664525u + 1013904223u;
        h = seed;
        s = (NtosAbiS3){b, c, d};
        t = (NtosAbiBytes3){(uint8_t)b, (uint8_t)(c >> 8), (uint8_t)(d >> 16)};
        q = ((uint64_t)c << 32) | d;
        CHECK(1, nt_msvc_imp_abi_typed_5(a, b, c, d, e) == nt_native_abi_typed_5(a, b, c, d, e));
        CHECK(2, nt_msvc_imp_abi_typed_8(a, b, c, d, e, f, g, h) ==
                     nt_native_abi_typed_8(a, b, c, d, e, f, g, h));
        CHECK(3, nt_msvc_imp_abi_typed_s3(a, s, e) == nt_native_abi_typed_s3(a, s, e));
        CHECK(4, nt_msvc_imp_abi_typed_s3split(a, b, c, s, e) ==
                     nt_native_abi_typed_s3split(a, b, c, s, e));
        CHECK(5, nt_msvc_imp_abi_typed_u64(a, q, e) == nt_native_abi_typed_u64(a, q, e));
        CHECK(6, nt_msvc_imp_abi_typed_u64split(a, b, c, q) ==
                     nt_native_abi_typed_u64split(a, b, c, q));
        sr = nt_msvc_imp_abi_typed_ret_s3(a, b);
        gr = nt_native_abi_typed_ret_s3(a, b);
        CHECK(7, sr.a == gr.a && sr.b == gr.b && sr.c == gr.c);
        rr = nt_msvc_imp_abi_typed_ret_u64(a);
        CHECK(8, rr == nt_native_abi_typed_ret_u64(a));
        tr = nt_msvc_imp_abi_typed_ret_bytes3(a);
        gt = nt_native_abi_typed_ret_bytes3(a);
        CHECK(9, tr.a == gt.a && tr.b == gt.b && tr.c == gt.c);
        CHECK(10, nt_msvc_imp_abi_typed_bytes3(t, e) == nt_native_abi_typed_bytes3(t, e));
        CHECK(11,
              nt_msvc_imp_abi_typed_ret_s16((int16_t)a) == nt_native_abi_typed_ret_s16((int16_t)a));
        CHECK(12, nt_msvc_imp_abi_typed_ret_u16((uint16_t)a) ==
                      nt_native_abi_typed_ret_u16((uint16_t)a));
        slot = a;
        ptr = &slot;
        CHECK(13, nt_msvc_imp_abi_typed_ptr(ptr) == ptr);
        CHECK(14, nt_msvc_imp_abi_typed_zero() == nt_native_abi_typed_zero());
        b1 = nt_msvc_imp_abi_typed_ret_bytes1(a);
        g1 = nt_native_abi_typed_ret_bytes1(a);
        CHECK(15, b1.a == g1.a);
        b4 = nt_msvc_imp_abi_typed_ret_bytes4(a);
        g4 = nt_native_abi_typed_ret_bytes4(a);
        CHECK(16, b4.a == g4.a && b4.b == g4.b && b4.c == g4.c && b4.d == g4.d);
        CHECK(17, nt_msvc_imp_abi_typed_ret_s8((int8_t)a) == nt_native_abi_typed_ret_s8((int8_t)a));
        CHECK(18,
              nt_msvc_imp_abi_typed_ret_u8((uint8_t)a) == nt_native_abi_typed_ret_u8((uint8_t)a));
    }
    return (int)0xAB1C0600u;
}
