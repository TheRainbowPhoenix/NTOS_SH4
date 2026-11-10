/* freestanding libc shim for zig -nostdlib PE builds (and the SH4 device) */
#ifndef SHIM_STDLIB_H
#define SHIM_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void free(void *p);
void *realloc(void *p, size_t n);
void abort(void);
#define SHIM_PROVIDED 1
#endif
