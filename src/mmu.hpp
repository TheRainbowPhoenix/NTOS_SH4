// mmu.hpp -- SH7305 (SH4A) MMU/UTLB control for the NTOS loader.
//
// Encodings checked against CP400 OS_02.01.7002.fls.c mmu_enable(),
// mmu_disable(), mmu_replace_entry_4k(), and gint SH7305 MMU definitions:
//
//   MMU_EnableAddressTranslation      -> MMUCR = 0x00FC0101
//   MMU_DisableAddinAddressSpace      -> MMUCR = 0x00FC0104 (bit 2 = TI,
//                                        TLB invalidate, hardware auto-clears)
//   AT is bit 0; bit 8 (0x100) is SV single virtual mode.
//   UTLB install                      -> URC = slot, PTEH = va,
//                                        PTEL = pa | 0x6C | 0x110 (= 0x17C),
//                                        ldtlb, CCR |= 1 around it.
//
// PTEL 0x17C = V(bit8) | PR=3(bits6:5) | SZ2(bit4, 4 KiB)
//             | C(bit3) | D(bit2); SH(bit1)=0 and WT(bit0)=0.
// See gint's SH7305 utlb_data_t and the CP400 firmware's
// mmu_replace_entry_4k(): it ORs the encoded flags with 0x110.
// This is writable, cached and dirty, with separate ASID sharing disabled.
//
// SH7305 has 64 fully associative UTLB entries selected by MMUCR.URC
// (bits 15:10), with an independent 4-entry ITLB. MMUCR.URB is bits
// 23:18, and ITLB replacement history LRUI is bits 31:26.
// MMUCR bits 5:4 are RESERVED on SH7305 (they select RC/way on SH7705).
// Never write a SH7705 way value here. Both CP400 ROM 02.01.7002 and
// OS330 ROM clear URC with 0xFFFF03FF, then insert index << 10.
#pragma once

#include <stdint.h>

namespace mmu {

// P4-area control registers (SH7305)
constexpr uint32_t REG_PTEH  = 0xFF000000u;
constexpr uint32_t REG_PTEL  = 0xFF000004u;
constexpr uint32_t REG_MMUCR = 0xFF000010u;
constexpr uint32_t REG_CCR   = 0xFF00001Cu;
constexpr uint32_t REG_TEA   = 0xFF00000Cu;
constexpr uint32_t REG_EXPEVT= 0xFF000024u;

// SH7305 MMUCR (see gint/mpu/mmu.h, SH7305 block; not SH7705).
constexpr uint32_t MMUCR_AT       = 0x00000001u;
constexpr uint32_t MMUCR_TI       = 0x00000004u;
constexpr uint32_t MMUCR_URC_MASK = 0x0000FC00u;
constexpr uint32_t MMUCR_URB_MASK = 0x00FC0000u;
constexpr uint32_t MMUCR_LRUI_MASK= 0xFC000000u;
constexpr uint32_t MMUCR_RESERVED_5_4 = 0x00000030u;
constexpr uint32_t UTLB_ENTRIES=64u;
static_assert((MMUCR_URC_MASK>>10)==0x3fu);
static_assert((MMUCR_URB_MASK>>18)==0x3fu);
static_assert((MMUCR_LRUI_MASK>>26)==0x3fu);

// OS330 baseline values (see header comment)
constexpr uint32_t MMUCR_ENABLE  = 0x00FC0101u; // AT on
constexpr uint32_t MMUCR_DISABLE = 0x00FC0104u; // AT off + TLB invalidate

// UTLB slot layout (OS330): 0x00-0x36 rotating add-in pages, 0x37-0x3A the
// 0x08100000 API window, 0x3F the null-page guard.
constexpr uint32_t SLOT_ROTATING_LAST = 0x36u;
constexpr uint32_t SLOT_API_FIRST     = 0x37u;
constexpr uint32_t SLOT_API_LAST      = 0x3Au;
constexpr uint32_t SLOT_NULL_GUARD    = 0x3Fu;

// Page entry flags: 4 KiB, shared, user RW, cacheable, valid.
constexpr uint32_t PTEL_PAGE = 0x0000017Cu;

// API window virtual address (OS330 C1A contract): first word of the window
// points at the syscall table.
constexpr uint32_t VA_API_WINDOW = 0x08100000u;
constexpr uint32_t API_WINDOW_PAGES = 4; // 0x37..0x3A -> 16 KiB

// Physical address mask used by OS330 (Bfile_GetBackingAddress results are
// masked the same way before landing in PTEL).
constexpr uint32_t PA_MASK = 0x1FFFFFFFu;

// --- assembly primitives (src/tlb_helper.S) ---------------------------------
extern "C" {
void tlb_install_entry(uint32_t va, uint32_t pa, uint32_t pte_flags,
                       uint32_t slot);
void tlb_set_urc(uint32_t slot);
void cache_invalidate_all(void);
void cache_flush_invalidate_block(void *addr, uint32_t size);
}

// --- C helpers --------------------------------------------------------------
inline uint32_t read_mmucr(void) {
    return *reinterpret_cast<volatile uint32_t *>(REG_MMUCR);
}

inline bool at_enabled(void) {
    return (read_mmucr() & MMUCR_AT) != 0;
}
inline uint32_t read_urc(void) {
    return (read_mmucr() & MMUCR_URC_MASK)>>10;
}
inline uint32_t read_urb(void) {
    return (read_mmucr() & MMUCR_URB_MASK)>>18;
}
inline uint32_t read_lrui(void) {
    return (read_mmucr() & MMUCR_LRUI_MASK)>>26;
}

// Enable address translation (OS330 MMU_EnableAddressTranslation).
inline void enable_at(void) {
    *reinterpret_cast<volatile uint32_t *>(REG_MMUCR) = MMUCR_ENABLE;
    cache_invalidate_all();
}

// Disable address translation + flush the whole UTLB
// (OS330 MMU_DisableAddinAddressSpace / MMU_RequestTLBFlushAndResetSlots).
inline void disable_at_flush(void) {
    *reinterpret_cast<volatile uint32_t *>(REG_MMUCR) = MMUCR_DISABLE;
    cache_invalidate_all();
}

// Flush the UTLB without touching AT (MMUCR |= TI).
inline void flush_tlb(void) {
    *reinterpret_cast<volatile uint32_t *>(REG_MMUCR) =
        read_mmucr() | 0x00000004u;
    cache_invalidate_all();
}

// Install one 4 KiB page mapping at a fixed UTLB slot. `pa` is a
// physical page base, generally derived from a P1 address using PA_MASK.
// The legacy final parameter is ignored: SH7305 has no 4-way UTLB index.
inline void map_page_at_index(uint32_t va, uint32_t pa, uint32_t slot,
                              uint32_t /*unused_way*/) {
    // Protect the fixed API and rotating mappings from accidental slot
    // overflow. The caller owns slot allocation, and this never infers
    // a slot from VPN bits.
    if(slot>=UTLB_ENTRIES) return;
    tlb_install_entry(va, pa & PA_MASK, PTEL_PAGE, slot);
}

// Opt-in diagnostic only. gint's SH7305 (NOT SH7705) arrays expose
// UTLB[entry] at F6000000/F7000000 + entry*0x100. Some emulators may
// reject P4 array reads; keep this out of the default dist_pager image.
#ifndef NTOS_VERIFY_UTLB_SLOTS
#define NTOS_VERIFY_UTLB_SLOTS 0
#endif
#if NTOS_VERIFY_UTLB_SLOTS && defined(__sh__)
inline bool verify_slot(uint32_t slot,uint32_t va,uint32_t pa) {
    if(slot>=UTLB_ENTRIES)return false;
    const uint32_t addr=*reinterpret_cast<volatile const uint32_t *>(
        0xF6000000u+(slot<<8));
    const uint32_t data=*reinterpret_cast<volatile const uint32_t *>(
        0xF7000000u+(slot<<8));
    // The P4 array stores VPN in bits 31:10, valid in bit 8.
    // The translated 4 KiB physical PPN is in data bits 28:12.
    return (addr&0xFFFFF000u)==(va&0xFFFFF000u) &&
           (addr&0x00000100u)!=0u &&
           (data&0x00000100u)!=0u &&
           (data&0x1FFFF000u)==(pa&0x1FFFF000u) &&
           (data&PTEL_PAGE)==PTEL_PAGE;
}
#endif

// Physical address of a P1 (cached identity) pointer.
inline uint32_t phys_addr(const void *p) {
    return reinterpret_cast<uint32_t>(p) & PA_MASK;
}

} // namespace mmu
