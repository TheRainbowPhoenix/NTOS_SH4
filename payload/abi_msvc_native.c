/* Shared native-GCC implementations for both ABI-bridge experiments.
 * Only five/eight 32-bit integer arguments are currently supported.
 */
typedef unsigned int U32;
U32 ntos_msvc_native5(U32 a, U32 b, U32 c, U32 d, U32 e)
{
    return 0x12345000u + 3u * a + 4u * b + 5u * c + 6u * d + 7u * e;
}
U32 ntos_msvc_native8(U32 a, U32 b, U32 c, U32 d, U32 e, U32 f, U32 g, U32 h)
{
    return ntos_msvc_native5(a, b, c, d, e) + 8u * f + 9u * g + 10u * h;
}
int main(void)
{
    return 1;
}
