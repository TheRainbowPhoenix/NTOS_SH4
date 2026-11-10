/* shim.c — freestanding libc bits for the zig -nostdlib PE build.
 *
 * The device has no libc; mquickjs needs exactly this surface. malloc is a
 * bump allocator over a fixed arena (same model as payload/phone/tiny_memory.c
 * on NTOS: no free-list, predictable memory, no fragmentation surprises).
 * printf-family calls inside mquickjs are debug paths that compile out.
 */
#include <stddef.h>
#include <stdint.h>

static int shim_contains(const void *p);

#define SHIM_ARENA (64 * 1024)
static unsigned char shim_arena[SHIM_ARENA] __attribute__((aligned(16)));
static size_t shim_used;

void *malloc(size_t n)
{
    n = (n + 15u) & ~(size_t)15u;
    if (shim_used + n > SHIM_ARENA)
        return 0;
    void *p = shim_arena + shim_used;
    shim_used += n;
    return p;
}

void free(void *p)
{
    (void)p; /* bump allocator: no-op, matching the NTOS payload model */
}

void *realloc(void *p, size_t n)
{
    if (!p)
        return malloc(n);
    void *q = malloc(n);
    if (q) {
        /* we do not know the old size; copy conservatively */
        unsigned char *d = q;
        const unsigned char *s = p;
        size_t i = 0;
        while (i < n && shim_contains(s + i)) {
            d[i] = s[i];
            ++i;
        }
    }
    return q;
}

static int shim_contains(const void *p)
{
    return (const unsigned char *)p >= shim_arena &&
           (const unsigned char *)p < shim_arena + SHIM_ARENA;
}

void abort(void)
{
    for (;;)
        ;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dd = d;
    const unsigned char *ss = s;
    while (n--)
        *dd++ = *ss++;
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *dd = d;
    while (n--)
        *dd++ = (unsigned char)c;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dd = d;
    const unsigned char *ss = s;
    if (dd < ss) {
        while (n--)
            *dd++ = *ss++;
    } else {
        dd += n;
        ss += n;
        while (n--)
            *--dd = *--ss;
    }
    return d;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        ++a;
        ++b;
        --n;
    }
    return n ? (int)(unsigned char)*a - (int)(unsigned char)*b : 0;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a;
    const unsigned char *y = b;
    while (n--) {
        if (*x != *y)
            return (int)*x - (int)*y;
        ++x;
        ++y;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (; *s; ++s)
        if (*s == (char)c)
            return (char *)s;
    return 0;
}

/* silent stdio stubs (engine debug paths only) */
int printf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}
int fprintf(void *f, const char *fmt, ...)
{
    (void)f;
    (void)fmt;
    return 0;
}
int puts(const char *s)
{
    (void)s;
    return 0;
}
