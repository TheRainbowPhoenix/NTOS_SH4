/* User-supplied msvc_abi_probe.c, with chat escaping removed. */
typedef unsigned long U32;
__declspec(noinline) U32 abi_1(U32 a) { return a + 1; }
__declspec(noinline) U32 abi_4(U32 a,U32 b,U32 c,U32 d) { return a ^ b ^ c ^ d; }
__declspec(noinline) U32 abi_5(U32 a,U32 b,U32 c,U32 d,U32 e) { return a+b+c+d+e; }
__declspec(noinline) U32 abi_8(U32 a,U32 b,U32 c,U32 d,U32 e,U32 f,U32 g,U32 h) { return a+b+c+d+e+f+g+h; }
typedef struct { U32 a,b,c; } ABI_STRUCT;
__declspec(noinline) ABI_STRUCT abi_struct(U32 x,U32 y) {
 ABI_STRUCT s; s.a=x; s.b=y; s.c=x^y; return s;
}
__declspec(noinline) U32 abi_caller(U32 x) { return abi_5(x,2,3,4,5); }
