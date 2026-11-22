/* XD-A8500 ROM32A firmware MMU contract, reconstructed from uploaded
 * ROM32A.BIN.c and checked against ROM32A.BIN (SHA256 224df7b13ea2...).
 *
 * Firmware's MMU_HandleTranslationFault:
 *   0 .. 0x6fffffff: (VA & 0x007ff000) | 0x10000000, flags 0x6c
 *   0x70000000 .. 0x73ffffff: add-in primary range, flags 0x4c
 *   0x74000000 .. 0x77ffffff: add-in API range, flags 0x6c
 * MMU_InstallTLBEntry ORs 0x110 (V + 4KiB page) into flags.
 *
 * Read-only model ONLY. This code never reads or writes P4 MMU registers,
 * changes active firmware mappings, or installs an exception handler.
 */
#ifndef NTOS_EXWORD_ROM32A_MMU_HPP
#define NTOS_EXWORD_ROM32A_MMU_HPP
#include <stdint.h>
namespace exword_rom32a {
constexpr uint32_t PAGE_SIZE=4096u;
constexpr uint32_t ADDIN_PRIMARY_MIN=0x70000000u;
constexpr uint32_t ADDIN_API_MIN=0x74000000u;
constexpr uint32_t ADDIN_API_MAX=0x78000000u;
constexpr uint32_t MMUCR_DISABLE=0x104u;
constexpr uint32_t MMUCR_ENABLE=0x101u;
constexpr uint32_t PTEL_PRIMARY=0x15cu; // 0x4c | 0x110: PR=10 RO
constexpr uint32_t PTEL_API=0x17cu;     // 0x6c | 0x110: PR=11 RW
enum class Zone: uint8_t { unmapped, firmware_alias, addin_primary, addin_api };
struct Windows {
    uint32_t primary_va,primary_end,primary_pa;
    uint32_t api_va,api_end,api_pa;
};
struct Mapping {
    Zone zone=Zone::unmapped;
    uint32_t physical=0u,ptel_flags=0u;
    bool valid() const { return zone!=Zone::unmapped; }
};
inline bool valid(const Windows &w) {
    return w.primary_va>=ADDIN_PRIMARY_MIN && w.primary_va<ADDIN_API_MIN &&
           w.primary_end>w.primary_va && w.primary_end<=ADDIN_API_MIN &&
           w.api_va>=ADDIN_API_MIN && w.api_va<ADDIN_API_MAX &&
           w.api_end>w.api_va && w.api_end<=ADDIN_API_MAX &&
           (w.primary_pa&4095u)==0u && (w.api_pa&4095u)==0u;
}
inline Mapping resolve(uint32_t va,const Windows &w) {
    if(va<ADDIN_PRIMARY_MIN) {
        /* The ROM maps these through a limited 8MiB device alias.
         * It is NOT NTOS-owned memory and must never be used as a VM
         * heap, even when the numeric translation is computable. */
        return {Zone::firmware_alias,
                0x10000000u|(va&0x007fffffu),PTEL_API};
    }
    if(!valid(w))return {};
    if(va>=w.primary_va && va<w.primary_end) {
        uint32_t offset=va-w.primary_va;
        if(w.primary_pa>0xffffffffu-offset)return {};
        return {Zone::addin_primary,w.primary_pa+offset,PTEL_PRIMARY};
    }
    if(va>=w.api_va && va<w.api_end) {
        uint32_t offset=va-w.api_va;
        if(w.api_pa>0xffffffffu-offset)return {};
        return {Zone::addin_api,w.api_pa+offset,PTEL_API};
    }
    return {};
}
/* The D01's native guest code executes from owned P1 SRAM, and *does not*
 * enter the ROM's P0 primary/API address space. Never mark unleased P0
 * pages safe just because firmware could map them for the host add-in. */
inline bool guest_owned(uint32_t ptr,uint32_t length,
                        uint32_t image_start,uint32_t image_end,
                        uint32_t stack_start,uint32_t stack_end) {
    if(!length || ptr>0xffffffffu-length)return false;
    return (ptr>=image_start && ptr<=image_end &&
            length<=image_end-ptr) ||
           (ptr>=stack_start && ptr<=stack_end &&
            length<=stack_end-ptr);
}
inline int32_t guest_hint(uint32_t ptr,uint32_t image_start,
                          uint32_t image_end,uint32_t stack_start,
                          uint32_t stack_end) {
    return guest_owned(ptr,1u,image_start,image_end,
                       stack_start,stack_end)?0:-2;
}
static_assert(PTEL_PRIMARY==(0x4cu|0x110u),"ROM RO flags");
static_assert(PTEL_API==(0x6cu|0x110u),"ROM RW flags");
} // namespace exword_rom32a
#endif
