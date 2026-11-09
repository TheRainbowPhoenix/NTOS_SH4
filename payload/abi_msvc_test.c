/* Distinct PE import/export tests for fixed-word MSVC SH4 calling rules.
 * The imported IAT slots are recognized by elf2ntx.py as __imp_*.
 */
#include "ntimport.h"
typedef unsigned int U32;
NT_DEFINE(abi_msvc5)
NT_DEFINE(abi_msvc8)

#if NTOS_MSVC_TEST_GLOBAL
/* Generic runtime dispatch reads arguments, then makes an MSVC call. */
extern U32 ntos_msvc_call_words(const void *fn,const U32 *a,U32 count);
static U32 do5(void)
{
    const U32 args[5]={1u,2u,3u,4u,5u};
    return ntos_msvc_call_words((const void *)__imp_abi_msvc5,args,5u);
}
static U32 do8(void)
{
    const U32 args[8]={1u,2u,3u,4u,5u,6u,7u,8u};
    return ntos_msvc_call_words((const void *)__imp_abi_msvc8,args,8u);
}
#else
/* Generator emits an ordinary GCC-callable import thunk for each symbol. */
extern U32 ntos_msvc_import5(U32,U32,U32,U32,U32);
extern U32 ntos_msvc_import8(U32,U32,U32,U32,U32,U32,U32,U32);
static U32 do5(void)
{
    return ntos_msvc_import5(1u,2u,3u,4u,5u);
}
static U32 do8(void)
{
    return ntos_msvc_import8(1u,2u,3u,4u,5u,6u,7u,8u);
}
#endif

int main(void)
{
    if(do5()!=0x12345055u)return (int)0xAB100005u;
    if(do8()!=0x12345114u)return (int)0xAB100008u;
#if NTOS_MSVC_TEST_GLOBAL
    return (int)0xAB1C0001u;
#else
    return (int)0xAB1C0002u;
#endif
}
