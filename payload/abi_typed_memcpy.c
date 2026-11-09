/* Freestanding support for the isolated aggregate ABI test only.
 * GCC is allowed to lower a 12-byte struct copy/return to memcpy.
 * Deliberately not part of the main NTOS runtime or the pager.
 */
#include <stddef.h>
void *memcpy(void *dest,const void *src,size_t n)
{
    unsigned char *d=(unsigned char *)dest;
    const unsigned char *s=(const unsigned char *)src;
    for(size_t i=0;i<n;++i)d[i]=s[i];
    return dest;
}
