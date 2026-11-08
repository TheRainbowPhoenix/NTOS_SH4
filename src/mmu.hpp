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
// PTEL 0x17C = SH(bit6) | PR1(bit5) | PR0(bit4) | D(bit3) | C(bit2)
//             | SZ0(bit4, 4 KiB) | V(bit8).
// WinCE's mem_shx.h arrives at the same value for a 4 KiB user RW cacheable
// page (PG_4K|PG_VALID|PG_DIRTY|PG_CACHE|PG_PROT_WRITE = 0x17D -- WinCE also
// sets the WT bit 0, which OS330 leaves clear); the bit *positions* differ
// per CPU so we follow the hardware we actually run on (SH7305).
//
// SH7305 has 64 fully associative UTLB entries, selected by MMUCR.URC
// (bits 15:10), and four ITLB entries. MMUCR bits 5:4 are hardware
// instruction-TLB LRU state, NOT a 4-way UTLB selector. CP400 firmware
// writes only URC for LDTLB, preserving those low bits.
#pragma once

#include <stdint.h>

namespace mmu {

// P4-area control registers (SH7305)
constexpr uint32_t REG_PTEH  = 0xFF000000u;
constexpr uint32_t REG_PTEL  = 0xFF000004u;
constexpr uint32_t REG_MMUCR = 0xFF000010u;
constexpr uint32_t REG_CCR   = 0xFF00001Cu;

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

inline bool at_enabled(void) { return (read_mmucr() & 1u) != 0; }

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
    tlb_install_entry(va, pa & PA_MASK, PTEL_PAGE, slot);
}

// Physical address of a P1 (cached identity) pointer.
inline uint32_t phys_addr(const void *p) {
    return reinterpret_cast<uint32_t>(p) & PA_MASK;
}

} // namespace mmu
