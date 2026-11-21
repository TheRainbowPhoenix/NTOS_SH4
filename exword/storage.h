/* EX-word HAL: bounded, read-only volume enumeration.
 * Never call these native firmware APIs without gint_world_switch().
 * Path spelling comes from the verified Edelweiss EX-word backend.
 */
#ifndef NTOS_EXWORD_STORAGE_H
#define NTOS_EXWORD_STORAGE_H
#include <stdint.h>
#define NTOS_EXWORD_LIST_MAX 8u
#define NTOS_EXWORD_NAME_MAX 48u
struct ntos_exword_listing {
    unsigned count;
    int result;
    char names[NTOS_EXWORD_LIST_MAX][NTOS_EXWORD_NAME_MAX];
    uint32_t kinds[NTOS_EXWORD_LIST_MAX];
};
static inline const char *ntos_exword_pattern(unsigned volume)
{
    return volume == 0u ? "\\\\drv0\\*" :
           volume == 1u ? "\\\\crd0\\*" : (const char *)0;
}
#ifdef __cplusplus
extern "C" {
#endif
int ntos_exword_scan(unsigned volume, struct ntos_exword_listing *out);
#ifdef __cplusplus
}
#endif
#endif
