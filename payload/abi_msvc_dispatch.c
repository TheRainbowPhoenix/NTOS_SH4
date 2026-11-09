/* Native GCC dispatcher for the shared MSVC-entry assembly trampoline.
 * Supports only scalar 32-bit word signatures of arity 5 or 8.
 * Callers supply an eight-word snapshot; there is no type inference.
 */
typedef unsigned int U32;
typedef U32 (*Fn5)(U32,U32,U32,U32,U32);
typedef U32 (*Fn8)(U32,U32,U32,U32,U32,U32,U32,U32);
__attribute__((noinline))
U32 ntos_msvc_dispatch_native(const void *target,const U32 *a,U32 count)
{
    if(count==5u)
        return ((Fn5)target)(a[0],a[1],a[2],a[3],a[4]);
    if(count==8u)
        return ((Fn8)target)(a[0],a[1],a[2],a[3],
                             a[4],a[5],a[6],a[7]);
    return 0xBADAB100u;
}
