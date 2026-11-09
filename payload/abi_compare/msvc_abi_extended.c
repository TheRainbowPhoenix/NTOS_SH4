/* Additional boundary fixtures; not part of the user's original probe. */
#include <stdarg.h>
typedef unsigned long U32;
typedef unsigned __int64 U64;
typedef struct { U32 a,b,c; } S3;
typedef struct { unsigned char a; } S1;
typedef struct { unsigned char a,b,c; } B3;
typedef struct { U32 a; } S4;
typedef struct { U32 a,b; } S8;
extern U32 opaque(U32);
extern float opaque_f(float);
extern void opaque_void(void);
extern U32 __cdecl target_cdecl(U32,U32,U32,U32,U32);
extern U32 __stdcall target_stdcall(U32,U32,U32,U32,U32);
U32 abi_preserve(const U32 *p) {
 U32 a=p[0]+1,b=p[1]+2,c=p[2]+3,d=p[3]+4,e=p[4]+5,f=p[5]+6,g=p[6]+7,h=p[7]+8;
 U32 i=p[8]+9,j=p[9]+10,k=p[10]+11,l=p[11]+12,m=p[12]+13,n=p[13]+14;
 U32 r=opaque(a);return r+a+b+c+d+e+f+g+h+i+j+k+l+m+n;
}
float abi_preserve_fp(const float *p) {
 float a=p[0],b=p[1],c=p[2],d=p[3],e=p[4],f=p[5];
 float r=opaque_f(a);return r+a+b+c+d+e+f;
}
U32 abi_bigframe(U32 x){volatile U32 a[128];a[x&127]=x;opaque_void();return a[x&127];}
U32 abi_split(U32 a,U32 b,U32 c,S3 s,U32 z){return a+b+c+s.a+s.b+s.c+z;}
S1 abi_ret1(U32 x){S1 s;s.a=(unsigned char)x;return s;}
B3 abi_ret3(U32 x){B3 s;s.a=(unsigned char)x;s.b=2;s.c=3;return s;}
S4 abi_ret4(U32 x){S4 s;s.a=x;return s;}
S8 abi_ret8(U32 x,U32 y){S8 s;s.a=x;s.b=y;return s;}
U64 abi_ret64(const U64 *p){return *p;}
U32 abi_arg64(U32 a,U64 b,U32 c){return a+(U32)b+c;}
unsigned char abi_u8(U32 x){return (unsigned char)x;}
signed char abi_s8(U32 x){return (signed char)x;}
unsigned short abi_u16(U32 x){return (unsigned short)x;}
short abi_s16(U32 x){return (short)x;}
U32 abi_var(U32 n,...){va_list ap;U32 a,b,c,d,e;va_start(ap,n);a=va_arg(ap,U32);b=va_arg(ap,U32);c=va_arg(ap,U32);d=va_arg(ap,U32);e=va_arg(ap,U32);va_end(ap);return n+a+b+c+d+e;}
extern U32 target_var(U32,...);
U32 abi_callvar(void){return target_var(1,2,3,4,5,6);}
U32 abi_callvar_fp(void){return target_var(1,2.0f,3.0,4);}
extern U32 unprototyped();
U32 abi_call_unprototyped(void){return unprototyped(1,2.0f,3.0,4);}
U32 abi_cdecl(void){return target_cdecl(1,2,3,4,5);}
U32 abi_stdcall(void){return target_stdcall(1,2,3,4,5);}
extern float target_float(U32,float,U32,float,U32);
float abi_fp_mix(void){return target_float(1,2.0f,3,4.0f,5);}
extern double target_double(U32,double,U32);
double abi_double_mix(void){return target_double(1,2.0,3);}
U32 abi_straddle64(U32 a,U32 b,U32 c,U64 d){return a+b+c+(U32)d;}
extern U32 target_fpvar(float,...);
U32 abi_named_fp_var(void){return target_fpvar(1.0f,2.0f,3);}
extern U32 target_f8(float,float,float,float,float,float,float,float);
U32 abi_call_f8(void){return target_f8(1,2,3,4,5,6,7,8);}
extern U32 target_f10(float,float,float,float,float,float,float,float,float,float);
U32 abi_call_f10(void){return target_f10(1,2,3,4,5,6,7,8,9,10);}
extern U32 target_after4(U32,U32,U32,U32,float);
U32 abi_fp_after4(void){return target_after4(1,2,3,4,5.0f);}
extern U32 target_fdf(float,double,float);
U32 abi_fdf(void){return target_fdf(1.0f,2.0,3.0f);}
typedef struct {float a,b;} F2;
extern U32 target_fstruct(F2,U32);
U32 abi_fp_struct(void){F2 s;s.a=1;s.b=2;return target_fstruct(s,3);}
void *_alloca(unsigned int);
#pragma intrinsic(_alloca)
U32 abi_dynamic(U32 n){volatile U32 *p=(volatile U32 *)_alloca(((n&15)+1)*4);p[0]=n;opaque_void();return p[0];}
