/* Import-free SH4-BE sample. NEWCPLINFOW is the WinCE 468-byte wire ABI. */
#include <stdint.h>
typedef struct {
    uint32_t size, flags, help, data, icon;
    uint16_t name[32], info[64], helpfile[128];
} NEWCPLINFO;
_Static_assert(sizeof(NEWCPLINFO) == 468, "CE NEWCPLINFOW");
static void wide(uint16_t *out, const char *in, unsigned capacity)
{
    unsigned n = 0;
    while (in[n] && n + 1 < capacity) {
        out[n] = (unsigned char)in[n];
        ++n;
    }
    out[n] = 0;
}
uint32_t DemoCPlApplet(void *owner, uint32_t message, uint32_t index, NEWCPLINFO *out)
{
    (void)owner;
    (void)index;
    if (message == 1 || message == 2)
        return 1;              /* INIT / GETCOUNT */
    if (message == 8 && out) { /* NEWINQUIRE: caller initially passes a zero size. */
        unsigned char *p = (unsigned char *)out;
        for (unsigned i = 0; i < sizeof(*out); ++i)
            p[i] = 0;
        out->size = sizeof(*out);
        out->icon = 2; /* synthetic icon accepted by this profile */
        wide(out->name, "NTOS Demo", 32);
        wide(out->info, "Sample SH4 big-endian control panel", 64);
        return 0;
    }
    if (message == 5 || message == 9) { /* DBLCLK / STARTWPARMS */
        const void *const *api = *(const void *const *volatile *)0x08100000u;
        if (api && api[6] && api[16] && api[2]) {
            ((void (*)(uint32_t))api[6])(0xffff);
            ((void (*)(uint32_t, uint32_t, const char *))api[16])(12, 32, "NTOS Demo");
            ((void (*)(uint32_t, uint32_t, const char *))api[16])(12, 64,
                                                                  "Hello from demo.cpl (SH4-BE)");
            ((void (*)(void))api[2])();
        }
        return 1;
    }
    return 0; /* STOP / EXIT / INQUIRE */
}
int main(void)
{
    return 1;
}

uint32_t cc_unimplemented(uint32_t ordinal)
{
    (void)ordinal;
    return 0;
}
