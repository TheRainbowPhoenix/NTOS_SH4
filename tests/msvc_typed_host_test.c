/* Host-side marshaling oracle. Does not replace SH4 emulator validation. */
#include <stdint.h>
#include "abi_typed.h"
#define CHECK(x,n) do { if (!(x)) return n; } while(0)
#define DECL(n) extern uint32_t nt_msvc_body_##n(const uint32_t *,void *)
DECL(abi_typed_5); DECL(abi_typed_8); DECL(abi_typed_s3);
DECL(abi_typed_s3split); DECL(abi_typed_u64); DECL(abi_typed_u64split);
DECL(abi_typed_ret_s3); DECL(abi_typed_ret_u64);
DECL(abi_typed_ret_bytes3); DECL(abi_typed_bytes3);
DECL(abi_typed_ret_s16); DECL(abi_typed_ret_u16); DECL(abi_typed_zero);
DECL(abi_typed_ret_bytes1); DECL(abi_typed_ret_bytes4);
DECL(abi_typed_ret_s8); DECL(abi_typed_ret_u8);
int main(void) {
    uint32_t w[8]={0};
    NtosAbiS3 s={0}; uint64_t u=0;
    w[0]=1;w[1]=2;w[2]=3;w[3]=4;w[4]=5;
    CHECK(nt_msvc_body_abi_typed_5(w,0)==105u,1);
    w[5]=6;w[6]=7;w[7]=8;
    CHECK(nt_msvc_body_abi_typed_8(w,0)==454u,2);
    w[0]=3;w[1]=4;w[2]=5;w[3]=6;w[4]=7;
    CHECK(nt_msvc_body_abi_typed_s3(w,0)==159u,3);
    w[0]=1;w[1]=2;w[2]=3;w[3]=4;w[4]=5;w[5]=6;w[6]=7;
    CHECK(nt_msvc_body_abi_typed_s3split(w,0)==302u,4);
    w[0]=3;w[1]=0x55667788u;w[2]=0x11223344u;w[3]=7;
    CHECK(nt_msvc_body_abi_typed_u64(w,0)==
          3u+3u*0x55667788u+5u*0x11223344u+49u,5);
    w[0]=1;w[1]=2;w[2]=3;w[3]=0xE5F60718u;w[4]=0xA1B2C3D4u;
    CHECK(nt_msvc_body_abi_typed_u64split(w,0)==
          1u+6u+15u+7u*0xE5F60718u+11u*0xA1B2C3D4u,6);
    w[1]=0x1234u;w[2]=0x4321u;
    (void)nt_msvc_body_abi_typed_ret_s3(w,&s);
    CHECK(s.a==0x1235u && s.b==0x4323u && s.c==(0x1234u^0x4321u),7);
    w[1]=0xABCDEFFFu;
    (void)nt_msvc_body_abi_typed_ret_u64(w,&u);
    CHECK(u==(UINT64_C(0xAABBCCDD00000000)|
              (uint64_t)(0xABCDEFFFu^0x13572468u)),8);
    w[0]=0x654321u;
    CHECK(nt_msvc_body_abi_typed_ret_bytes3(w,0)==0x654321u,9);
    w[0]=0x563412u;w[1]=99u;
    CHECK(nt_msvc_body_abi_typed_bytes3(w,0)==
          99u+0x12u+3u*0x34u+7u*0x56u,10);
    w[0]=(uint32_t)(int32_t)-123;
    CHECK((int16_t)nt_msvc_body_abi_typed_ret_s16(w,0)==-130,11);
    w[0]=65530;
    CHECK((uint16_t)nt_msvc_body_abi_typed_ret_u16(w,0)==65535,12);
    CHECK(nt_msvc_body_abi_typed_zero(w,0)==0xAB1C0420u,13);
    w[0]=0xA5u;
    CHECK(nt_msvc_body_abi_typed_ret_bytes1(w,0)==0xA5u,14);
    w[0]=0x12345678u;
    CHECK(nt_msvc_body_abi_typed_ret_bytes4(w,0)==0x12345678u,15);
    w[0]=(uint32_t)(int32_t)-123;
    CHECK((int8_t)nt_msvc_body_abi_typed_ret_s8(w,0)==126,16);
    w[0]=250u;
    CHECK((uint8_t)nt_msvc_body_abi_typed_ret_u8(w,0)==255u,17);
    return 0;
}
