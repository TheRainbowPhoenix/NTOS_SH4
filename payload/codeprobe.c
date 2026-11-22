/* Import-free, separately compiled CODEA/CODEB PE.
 * Both are independently rebased by stage0 to EXACT same P0 executable
 * VA and have separate P1-backed .text/.data. They are never invoked
 * alone: PSCODE.EXE selects which CPU-executed mapping is active.
 * The output high 16 bits proves which physical .text ran; the
 * separately incremented .bss counter proves which .data was selected.
 */
#include <stdint.h>
#ifndef NTOS_CODE_TAG
#error "set NTOS_CODE_TAG for either CODEA or CODEB"
#endif
static volatile uint32_t counter;
static volatile uint32_t signature=NTOS_CODE_TAG;
int main(const void *const *api,char **argv,char **envp) {
    (void)api;(void)envp;
    const uint32_t expected=(uint32_t)(uintptr_t)argv;
    if(signature!=NTOS_CODE_TAG || counter!=expected)
        return (int)0xBADCE001u;
    ++counter;
    signature=NTOS_CODE_TAG; // page stays mutable and private to this PE
    return (int)(NTOS_CODE_TAG | expected);
}
