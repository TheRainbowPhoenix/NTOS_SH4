/* Independent compiler fixtures. No compiler-specific ABI annotations. */
#include <stdarg.h>
#ifdef ABI_GCC_RENESAS
#define ABI __attribute__((renesas))
#else
#define ABI
#endif
typedef unsigned U;
#ifdef _MSC_VER
typedef unsigned __int64 U64;
#else
typedef unsigned long long U64;
#endif
typedef struct { U a,b,c; } S3;
typedef struct { U a; } S1;
volatile U abi_sink;
extern U ABI ext8(U,U,U,U,U,U,U,U);
extern U ABI extvar(U,...);
extern U ABI extstruct(U,S3,U);
extern S3 ABI extreturn(U,U,U);
extern U64 ABI ext64(U,U64,U);
extern float ABI extfloat(U,float,U,float,U);
extern double ABI extdouble(U,double,U);
U ABI take8(U a,U b,U c,U d,U e,U f,U g,U h){return a^(b<<1)^(c<<2)^(d<<3)^(e<<4)^(f<<5)^(g<<6)^(h<<7);}
U ABI call8(void){U r=ext8(11,22,33,44,55,66,77,88);abi_sink=r;return r;}
typedef U (*Callback8)(U,U,U,U,U,U,U,U) ABI;
U ABI callptr(Callback8 f){U r=f(11,22,33,44,55,66,77,88);abi_sink=r;return r;}
U ABI takestruct(U p,S3 s,U q){return p+s.a+s.b+s.c+q;}
U ABI callstruct(void){S3 s;U r;s.a=22;s.b=33;s.c=44;r=extstruct(11,s,55);abi_sink=r;return r;}
S3 ABI retstruct(U a,U b,U c){S3 s;s.a=a;s.b=b;s.c=c;return s;}
S1 ABI retone(U a){S1 s;s.a=a;return s;}
U ABI callreturn(void){S3 s=extreturn(11,22,33);abi_sink=s.a;return s.b+s.c;}
U64 ABI take64(U a,U64 b,U c){return b+(((U64)a)<<32)+c;}
U64 ABI call64(void){U64 r=ext64(11,(((U64)0x11223344)<<32)|0x55667788u,22);abi_sink=(U)r;return r;}
U ABI callvar(void){U r=extvar(11,22,33,44,55,66);abi_sink=r;return r;}
U ABI takevar(U n,...){va_list ap;U a,b,c,d,e;va_start(ap,n);a=va_arg(ap,U);b=va_arg(ap,U);c=va_arg(ap,U);d=va_arg(ap,U);e=va_arg(ap,U);va_end(ap);return n+a+b+c+d+e;}
float ABI callfloat(void){float r=extfloat(11,2.0f,33,4.0f,55);abi_sink=(U)r;return r;}
double ABI calldouble(void){double r=extdouble(11,2.0,33);abi_sink=(U)r;return r;}
U ABI layout(void){struct Layout{char a;double b;short c;};return sizeof(struct Layout)*256+sizeof(double)*16+sizeof(U64);}
U ABI take64arg(U a,U64 b,U c){return a+(U)b+c;}
U ABI call64arg(void){return take64arg(11,(((U64)0x11223344)<<32)|0x55667788u,22);}
U64 ABI ret64load(const U64 *p){return *p;}
U ABI offset_double(void){struct L{char a;double b;short c;};return (U)&((struct L *)0)->b;}
U ABI offset_short(void){struct L{char a;double b;short c;};return (U)&((struct L *)0)->c;}
