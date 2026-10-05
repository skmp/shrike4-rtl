! r5probes.S -- probes of r5warm.c (M2, agents/reports/integration/M2.md): copies of probes2.S's p_mjmp0 / p_mjmp1
! (MMUCR store, n NOPs, JSR @r6) pinned to a cache-line offset, so that the delay slot's line is known:
!   p_mjc0  n = 0 at mmutest2.elf's own layout (entry at line offset 0x14): store +0x1C, JSR +0x1E, slot in the NEXT line
!   p_mjc1  n = 1 at mmutest2.elf's own layout (entry at +0x10): store +0x18, NOP +0x1A, JSR +0x1C, slot +0x1E
!   p_mjm0  n = 0 mid-line (entry at +0x00): store +0x08, JSR +0x0A, slot +0x0C (same line)
!   p_mjn1  n = 1 with the slot in the next line (entry at +0x12): store +0x1A, NOP +0x1C, JSR +0x1E
! Same recovery protocol as probes.S / probes2.S (_g_recover <- label 90 at entry).  Every routine's registers:
! r4 = MMUCR's address, r5 = the value to write, r6 = the JSR target.
    .text
    .little

    .macro PBEGIN name
    .globl _\name
_\name:
    mov.l   91f, r1
    mov.l   92f, r2
    mov.l   r2, @r1
    .endm
    .macro PPOOL
    .align 2
91: .long _g_recover
92: .long 90b
    .endm
    .macro PAD n
    .rept \n
    nop
    .endr
    .endm

    .align 5
    PAD 10                      ! entry at +0x14
    PBEGIN p_mjc0               ! +0x14 +0x16 +0x18
    sts     pr, r7              ! +0x1A
    mov.l   r5, @r4             ! +0x1C
    jsr     @r6                 ! +0x1E
    nop                         ! next line +0x00
90:
    lds     r7, pr
    rts
    nop
    PPOOL

    .align 5
    PAD 8                       ! entry at +0x10
    PBEGIN p_mjc1               ! +0x10 +0x12 +0x14
    sts     pr, r7              ! +0x16
    mov.l   r5, @r4             ! +0x18
    nop                         ! +0x1A
    jsr     @r6                 ! +0x1C
    nop                         ! +0x1E
90:
    lds     r7, pr
    rts
    nop
    PPOOL

    .align 5
    PBEGIN p_mjm0               ! +0x00 +0x02 +0x04
    sts     pr, r7              ! +0x06
    mov.l   r5, @r4             ! +0x08
    jsr     @r6                 ! +0x0A
    nop                         ! +0x0C
90:
    lds     r7, pr
    rts
    nop
    PPOOL

    .align 5
    PAD 9                       ! entry at +0x12
    PBEGIN p_mjn1               ! +0x12 +0x14 +0x16
    sts     pr, r7              ! +0x18
    mov.l   r5, @r4             ! +0x1A
    nop                         ! +0x1C
    jsr     @r6                 ! +0x1E
    nop                         ! next line +0x00
90:
    lds     r7, pr
    rts
    nop
    PPOOL
