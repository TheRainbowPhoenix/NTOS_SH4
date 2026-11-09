/* Full typed cross-DLL ABI boundary test. Distinct failure IDs for emulator. */
#include "abi_typed.h"
#define CHECK(c, n)                                                                                \
    do {                                                                                           \
        if (!(c))                                                                                  \
            return (int)(n);                                                                       \
    } while (0)
int main(void)
{
    const NtosAbiS3 s = {4, 5, 6};
    const NtosAbiBytes3 b = {0x12u, 0x34u, 0x56u};
    NtosAbiS3 v;
    NtosAbiBytes3 c;
    uint32_t marker = 0x1234u;
    CHECK(nt_msvc_imp_abi_typed_5(1, 2, 3, 4, 5) == 105u, 0xAB1E0001u);
    CHECK(nt_msvc_imp_abi_typed_8(1, 2, 3, 4, 5, 6, 7, 8) == 454u, 0xAB1E0002u);
    CHECK(nt_msvc_imp_abi_typed_s3(3, s, 7) == 159u, 0xAB1E0003u);
    CHECK(nt_msvc_imp_abi_typed_s3split(1, 2, 3, s, 7) == 302u, 0xAB1E0004u);
    CHECK(nt_msvc_imp_abi_typed_u64(3, UINT64_C(0x1122334455667788), 7) ==
              3u + 3u * 0x55667788u + 5u * 0x11223344u + 49u,
          0xAB1E0005u);
    CHECK(nt_msvc_imp_abi_typed_u64split(1, 2, 3, UINT64_C(0xA1B2C3D4E5F60718)) ==
              1u + 6u + 15u + 7u * 0xE5F60718u + 11u * 0xA1B2C3D4u,
          0xAB1E0006u);
    v = nt_msvc_imp_abi_typed_ret_s3(0x1234u, 0x4321u);
    CHECK(v.a == 0x1235u && v.b == 0x4323u && v.c == (0x1234u ^ 0x4321u), 0xAB1E0007u);
    CHECK(nt_msvc_imp_abi_typed_ret_u64(0xABCDEFFFu) ==
              (UINT64_C(0xAABBCCDD00000000) | (uint64_t)(0xABCDEFFFu ^ 0x13572468u)),
          0xAB1E0008u);
    c = nt_msvc_imp_abi_typed_ret_bytes3(0x00654321u);
    CHECK(c.a == 0x21u && c.b == 0x43u && c.c == 0x65u, 0xAB1E0009u);
    CHECK(nt_msvc_imp_abi_typed_bytes3(b, 99u) == 99u + 0x12u + 3u * 0x34u + 7u * 0x56u,
          0xAB1E000Au);
    CHECK(nt_msvc_imp_abi_typed_ret_s16(-123) == -130, 0xAB1E000Bu);
    CHECK(nt_msvc_imp_abi_typed_ret_u16(65530u) == 65535u, 0xAB1E000Cu);
    CHECK(nt_msvc_imp_abi_typed_ptr(&marker) == &marker, 0xAB1E000Du);
    CHECK(nt_msvc_imp_abi_typed_zero() == 0xAB1C0420u, 0xAB1E000Eu);
    {
        NtosAbiBytes1 t = nt_msvc_imp_abi_typed_ret_bytes1(0xA5u);
        CHECK(t.a == 0xA5u, 0xAB1E000Fu);
    }
    {
        NtosAbiBytes4 t = nt_msvc_imp_abi_typed_ret_bytes4(0x12345678u);
        CHECK(t.a == 0x78u && t.b == 0x56u && t.c == 0x34u && t.d == 0x12u, 0xAB1E0010u);
    }
    CHECK(nt_msvc_imp_abi_typed_ret_s8(-123) == 126, 0xAB1E0011u);
    CHECK(nt_msvc_imp_abi_typed_ret_u8(250u) == 255u, 0xAB1E0012u);
    return (int)0xAB1C0421u;
}
