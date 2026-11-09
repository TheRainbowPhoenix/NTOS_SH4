#ifndef PHONE_IO_H
#define PHONE_IO_H
#include "phone.h"
typedef struct {
    U32 attrs, times[6], size_hi, size_lo, oid;
    W16 name[260];
} FindData;
IMP(void *, CreateFileW, (const W16 *, U32, U32, void *, U32, U32, void *))
IMP(int, ReadFile, (void *, void *, U32, U32 *, void *))
IMP(int, WriteFile, (void *, const void *, U32, U32 *, void *))
IMP(int, CloseHandle, (void *))
IMP(U32, GetFileSize, (void *, U32 *))
IMP(U32, SetFilePointer, (void *, int, int *, U32))
IMP(void *, FindFirstFileW, (const W16 *, FindData *))
IMP(int, FindNextFileW, (void *, FindData *))
IMP(int, FindClose, (void *))
static inline void wide_(W16 *d, const char *s)
{
    unsigned i = 0;
    while (s[i] && i < 95) {
        d[i] = (unsigned char)s[i];
        ++i;
    }
    d[i] = 0;
}
static inline void copy_(char *d, const char *s, unsigned cap)
{
    unsigned i = 0;
    if (!cap)
        return;
    while (s[i] && i + 1 < cap) {
        d[i] = s[i];
        ++i;
    }
    d[i] = 0;
}
static inline unsigned len_(const char *s)
{
    unsigned n = 0;
    while (s[n])
        ++n;
    return n;
}
static inline void *file_(const char *name, int writing)
{
    W16 w[96];
    wide_(w, name);
    return CALL(CreateFileW, (w, writing ? 0x40000000u : 0x80000000u, 1, 0, writing ? 2 : 3, 0, 0));
}
static inline int load_(const char *name, void *b, unsigned cap)
{
    void *f = file_(name, 0);
    U32 n = 0;
    if (f == (void *)(intptr_t)-1)
        return -1;
    int ok = CALL(ReadFile, (f, b, cap, &n, 0));
    CALL(CloseHandle, (f));
    return ok ? (int)n : -1;
}
static inline int save_(const char *name, const void *b, unsigned size)
{
    void *f = file_(name, 1);
    U32 n = 0;
    if (f == (void *)(intptr_t)-1)
        return 0;
    int ok = CALL(WriteFile, (f, b, size, &n, 0));
    CALL(CloseHandle, (f));
    return ok && n == size;
}
static inline unsigned be16_(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}
static inline U32 le32_(const unsigned char *p)
{
    return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
}
static inline void put16_(unsigned char *p, unsigned n)
{
    p[0] = (unsigned char)(n >> 8);
    p[1] = (unsigned char)n;
}
#endif
