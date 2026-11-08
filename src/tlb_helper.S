! tlb_helper.S -- UTLB programming primitives for the SH7305 (SH4A, BE).
!
! Port of recreate/src/coffloader/tlb_helper.asm (Renesas asmsh syntax) to
! GNU as, with the encodings taken verbatim from the ClassPad 330 OS330
! MMU_* cluster (verified in Ghidra against OS330P_03.10.7000.fls and the
! same cluster in the fx-CP400 OS_02.01.2000.fls):
!
!   MMUCR  0xFF000010   AT = bit 0 (0xFC0101 = on, 0xFC0104 = off + TI)
!   PTEH   0xFF000000   VA page (low 10 bits = ASID, we keep 0)
!   PTEL   0xFF000004   PA page | flags; OS330 writes pa|0x6C|0x110 = 0x17C
!   CCR    0xFF00001C   bit 0 set to invalidate caches (OS330's own flow)
!   URC    MMUCR bits 15:10, cleared with mask 0xFFFF03FF (decompiled value)
!
! ldtlb commits PTEH/PTEL into the UTLB entry selected by URC. Entry flags
! used by NTOS: 0x17C = SH|PR1|PR0|D|C|V, SZ=00 (4 KiB on SH4A).

    .section .text
    .align 2

! ---------------------------------------------------------------------------
! void tlb_install_entry(u32 va   [r4],
!                        u32 pa   [r5],
!                        u32 pte_flags [r6],
!                        u32 slot [r7])
!
! OS330 MMU_InstallTLBEntryRotating / MMU_InstallTLBEntryAtIndex recipe:
! URC=slot -> cache invalidate -> PTEH=va -> PTEL=pa|flags -> ldtlb ->
! cache invalidate.
!
! SH7305 UTLB is fully associative with 64 entries. MMUCR.URC selects
! the slot; there is no UTLB "way" field. Bits 5:4 are LRUI (ITLB LRU).
! Do NOT modify them. WinCE/gint/CP400 firmware use URC alone here.
! ---------------------------------------------------------------------------
    .global _tlb_install_entry
_tlb_install_entry:
    mov.l   r8, @-r15
    sts.l   pr, @-r15
    mov     r7, r8                  ! r8 = slot

    ! MMUCR: URC = slot (bits 15:10); preserve hardware-managed LRUI
    mov.l   .Lmmucr, r0
    mov.l   @r0, r1
    mov.l   .Lurcmask, r2
    and     r2, r1                  ! clear URC (bits 15:10)
    mov     r8, r3
    shll8   r3
    shll2   r3                      ! slot << 10
    or      r3, r1
    mov.l   r1, @r0

    ! CCR |= 1 (cache invalidate, hardware auto-clear)
    mov.l   .Lccr, r1
    mov.l   @r1, r0
    or      #1, r0
    mov.l   r0, @r1

    ! PTEH = va (ASID 0)
    mov.l   .Lpteh, r0
    mov.l   r4, @r0

    ! PTEL = pa | (flags & 0x1FF)
    mov.l   .Lptelflags, r2
    and     r2, r6
    mov     r5, r0
    or      r6, r0
    mov.l   .Lptel, r1
    mov.l   r0, @r1

    ldtlb                           ! commit into UTLB[URC]

    ! CCR |= 1 again (OS330 invalidates after LDTLB too)
    mov.l   .Lccr, r1
    mov.l   @r1, r0
    or      #1, r0
    mov.l   r0, @r1

    lds.l   @r15+, pr
    mov.l   @r15+, r8
    rts
    nop

! ---------------------------------------------------------------------------
! void tlb_set_urc(u32 slot [r4])
! ---------------------------------------------------------------------------
    .global _tlb_set_urc
_tlb_set_urc:
    mov.l   .Lmmucr, r0
    mov.l   @r0, r1
    mov.l   .Lurcmask, r2
    and     r2, r1
    shll8   r4
    shll2   r4
    or      r4, r1
    mov.l   r1, @r0
    rts
    nop

! ---------------------------------------------------------------------------
! void cache_invalidate_all(void)   -- CCR |= 1 (ICI), hardware auto-clears
! ---------------------------------------------------------------------------
    .global _cache_invalidate_all
_cache_invalidate_all:
    mov.l   .Lccr, r1
    mov.l   @r1, r0
    or      #1, r0
    mov.l   r0, @r1
    rts
    nop

! ---------------------------------------------------------------------------
! void cache_flush_invalidate_block(void *addr [r4], u32 size [r5])
!
! 32-byte line sweep of ocbi+icbi over [addr, addr+size), then synco
! (SH4A: synchronize after operand-cache ops).
! ---------------------------------------------------------------------------
    .global _cache_flush_invalidate_block
_cache_flush_invalidate_block:
    mov     #-32, r1
    mov     r4, r0
    and     r1, r0                  ! r0 = addr aligned down to 32
    add     r5, r4
    add     #-1, r4
    and     r1, r4                  ! r4 = last line aligned down
.Lblk:
    ocbi    @r0
    icbi    @r0
    add     #32, r0
    cmp/hi  r4, r0                  ! T = (r0 > r4)
    bf      .Lblk                   ! loop while r0 <= r4
    synco
    rts
    nop

    .align 2
.Lmmucr:    .long 0xFF000010
.Lpteh:     .long 0xFF000000
.Lptel:     .long 0xFF000004
.Lccr:      .long 0xFF00001C
.Lurcmask:  .long 0xFFFF03FF
 .Lptelflags:.long 0x000001FF
