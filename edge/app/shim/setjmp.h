#ifndef SHIM_SETJMP_H
#define SHIM_SETJMP_H
/* mquickjs only uses setjmp in the host REPL (readline) path; the engine
 * core does not. Provide a never-used stub that fails loudly if taken. */
typedef unsigned long long jmp_buf[16];
int setjmp(jmp_buf b);
int _setjmp(jmp_buf b);
void longjmp(jmp_buf b, int v);
#endif
