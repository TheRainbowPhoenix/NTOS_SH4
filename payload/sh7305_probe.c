/* SPDX-FileCopyrightText: 2026 Artem Novak
 * SPDX-License-Identifier: BSD-3-Clause
 * SH7305PROBE.EXE: freestanding SH4AL-DSP big-endian NTOS diagnostic.
 * NTOS passes its syscall table in r4. This EXE uses no gint or SDK calls.
 */
#include <stdint.h>

typedef void (*puts_fn)(const char *);
typedef struct {
    puts_fn puts;
} ntos_api_t;

static uint32_t reg32(uint32_t address)
{
    return *(volatile const uint32_t *)(uintptr_t)address;
}

static void hexline(puts_fn out, const char *name, uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[48];
    unsigned n = 0, i;
    while(name[n] && n < 28) {
        line[n] = name[n];
        ++n;
    }
    line[n++] = '=';
    line[n++] = '0';
    line[n++] = 'x';
    for(i = 0; i < 8; ++i) {
        line[n++] = hex[(value >> (28 - i * 4)) & 15u];
    }
    line[n] = '\0';
    out(line);
}

int main(const ntos_api_t *api, char **argv, char **envp)
{
    uint32_t mmucr, code_va, code_pa, code_word;
    (void)argv;
    (void)envp;
    if(!api || !api->puts) return 1;

    api->puts("SH7305 PROBE (SH4AL-DSP BE / NTOS)");
    hexline(api->puts, "PVR", reg32(0xff000030u));
    hexline(api->puts, "PRR", reg32(0xff000044u));
    hexline(api->puts, "CVR", reg32(0xff000040u));
    hexline(api->puts, "CPID", reg32(0xff000048u));
    mmucr = reg32(0xff000010u);
    hexline(api->puts, "MMUCR", mmucr);
    hexline(api->puts, "AT", mmucr & 1u);
    hexline(api->puts, "UTLB URC", (mmucr >> 10) & 63u);
    hexline(api->puts, "UTLB URB", (mmucr >> 18) & 63u);

    code_va = (uint32_t)(uintptr_t)&main;
    hexline(api->puts, "CODE VA", code_va);
    /* P0 is translated only when AT is on. Do not claim a physical
     * address for an NTOS-mapped P0 program without a UTLB lookup. */
    if(code_va >= 0x80000000u && code_va < 0xc0000000u) {
        code_pa = code_va & 0x1fffffffu;
        hexline(api->puts, "CODE PA", code_pa);
        code_word = reg32(code_va);
        hexline(api->puts, "VA WORD", code_word);
        hexline(api->puts, "P1 WORD", reg32(code_pa | 0x80000000u));
    }
    else {
        api->puts("CODE PA: P0 mapping (not resolved)");
    }

    api->puts("No TLB entries modified.");
    api->puts("External ROM file not mapped by this EXE.");
    api->puts("Results sent to NTOS console (not /results.txt).");
    return 0;
}
