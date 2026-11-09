/* Isolated real-COREDLL test of generated MSVC SH4 ABI stubs.
 * Does not replace the production COREDLL.DLL.
 * getkey is imported but intentionally not invoked to avoid blocking.
 */
#include "abi_typed.h"
int main(void) {
    void *p;
    nt_msvc_imp_coredll_puts((void *)"MSVC SH4 typed COREDLL ABI probe");
    p=nt_msvc_imp_coredll_malloc(64u);
    if (!p) return (int)0xAB1E0501u;
    ((volatile unsigned char *)p)[0]=0x5Au;
    ((volatile unsigned char *)p)[63]=0xA5u;
    if(((volatile unsigned char *)p)[0]!=0x5Au ||
       ((volatile unsigned char *)p)[63]!=0xA5u)
        return (int)0xAB1E0502u;
    nt_msvc_imp_coredll_free(p);
    return (int)0xAB1C0500u;
}
