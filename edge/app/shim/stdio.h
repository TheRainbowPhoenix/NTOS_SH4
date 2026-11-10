#ifndef SHIM_STDIO_H
#define SHIM_STDIO_H
/* no stdio on the freestanding target; mquickjs debug prints compile out.
 * dtoa.c has a few live printf calls under DBL_DEBUG-style guards plus one
 * fallback — route them to a no-op so the engine stays silent. */
typedef int FILE;
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int puts(const char *s);
#endif
