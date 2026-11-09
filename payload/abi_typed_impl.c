/* Native GCC implementations behind generated MSVC SH4 ABI export thunks. */
#include "abi_typed.h"
uint32_t nt_native_abi_typed_5(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e) {
    return a+3u*b+5u*c+7u*d+11u*e;
}
uint32_t nt_native_abi_typed_8(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f,uint32_t g,uint32_t h) {
    return a+3u*b+5u*c+7u*d+11u*e+13u*f+17u*g+19u*h;
}
uint32_t nt_native_abi_typed_s3(uint32_t p,NtosAbiS3 s,uint32_t q) {
    return p+3u*s.a+5u*s.b+7u*s.c+11u*q;
}
uint32_t nt_native_abi_typed_s3split(uint32_t a,uint32_t b,uint32_t c,NtosAbiS3 s,uint32_t z) {
    return a+3u*b+5u*c+7u*s.a+11u*s.b+13u*s.c+17u*z;
}
uint32_t nt_native_abi_typed_u64(uint32_t a,uint64_t b,uint32_t c) {
    return a+3u*(uint32_t)b+5u*(uint32_t)(b>>32)+7u*c;
}
uint32_t nt_native_abi_typed_u64split(uint32_t a,uint32_t b,uint32_t c,uint64_t d) {
    return a+3u*b+5u*c+7u*(uint32_t)d+11u*(uint32_t)(d>>32);
}
NtosAbiS3 nt_native_abi_typed_ret_s3(uint32_t x,uint32_t y) {
    NtosAbiS3 r={x+1u,y+2u,x^y};return r;
}
uint64_t nt_native_abi_typed_ret_u64(uint32_t x) {
    return UINT64_C(0xAABBCCDD00000000)|(uint64_t)(x^0x13572468u);
}
NtosAbiBytes3 nt_native_abi_typed_ret_bytes3(uint32_t x) {
    NtosAbiBytes3 r={(uint8_t)x,(uint8_t)(x>>8),(uint8_t)(x>>16)};return r;
}
uint32_t nt_native_abi_typed_bytes3(NtosAbiBytes3 s,uint32_t x) {
    return x+(uint32_t)s.a+3u*(uint32_t)s.b+7u*(uint32_t)s.c;
}
int16_t nt_native_abi_typed_ret_s16(int16_t x) {return (int16_t)(x-7);}
uint16_t nt_native_abi_typed_ret_u16(uint16_t x) {return (uint16_t)(x+5u);}
void *nt_native_abi_typed_ptr(void *p) {return p;}
uint32_t nt_native_abi_typed_zero(void) {return 0xAB1C0420u;}
#ifndef NTOS_MSVC_HOST_TEST
int main(void) {return 1;}
#endif
