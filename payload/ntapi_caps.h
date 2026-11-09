/* API-window metadata. Word zero remains the legacy NtosApi pointer. */
#ifndef NTOS_API_CAPS_H
#define NTOS_API_CAPS_H
#include <stdint.h>
#define NTOS_API_MAGIC 0x4e415049u /* NAPI */
#define NTOS_API_VERSION 1u
static inline int ntos_api_has_slot(const volatile uint32_t *window, uint32_t slot)
{
    /* Old loaders zero this reserved window area. Never inspect a slot
     * beyond the table until its published length has been checked. */
    return window && window[1] == NTOS_API_MAGIC && window[2] == NTOS_API_VERSION &&
           window[3] <= 128u && slot < window[3];
}
#endif
