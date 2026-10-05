/* excrace_asm.S -- a P4 store to an exception register right before an entry that writes it through excw (excrace.c).
 * Each function returns r0 = a value read after the entry and the handler (0 if unused).
 *   xr_expevt_trapa()   EXPEVT <- 0x777 (Iclk CCN store, its write at MA3), TRAPA #0x11 at once
 *   xr_tra_trapa()      TRA <- 0x3FC, TRAPA #0x22 at once
 *   xr_tra_illegal()    TRA <- 0x3F0, an illegal instruction at once (TRA is TRAPA's only)
 *   xr_expevt_slot()    BRA to a TRAPA with EXPEVT <- 0x555 in the slot
 *   xr_expevt_adrerr()  EXPEVT <- 0x666, a misaligned load at once
 *   xr_trapa_slot()     BRA with TRAPA in its slot: slot illegal instruction (0x1A0), SPC = the BRA
 *   xr_rte_slot_rd()    TRAPA, whose return reads EXPEVT in the RTE's slot (xr_rte_handler: a private VBR handler)
 *   xr_intevt<k>(sr)    k = 0..8: SR <- sr (IMASK 15 -> 0 with TMU0 pending), k NOPs, INTEVT <- 0x3E0, 16 NOPs;
 *                       returns INTEVT read afterwards */
        .text
        .balign 32
        .global _xr_expevt_trapa, _xr_tra_trapa, _xr_tra_illegal, _xr_expevt_slot, _xr_expevt_adrerr, _xr_trapa_slot
        .global _xr_resume, _xr_slot_bra
_xr_expevt_trapa:
        mov.l   .Lccn,r1
        mov.w   .L777,r0
        mov.l   r0,@(0x24,r1)
        trapa   #0x11
        bra     _xr_resume
        mov     #0,r0
_xr_tra_trapa:
        mov.l   .Lccn,r1
        mov.w   .L3fc,r0
        mov.l   r0,@(0x20,r1)
        trapa   #0x22
        bra     _xr_resume
        mov     #0,r0
_xr_tra_illegal:
        mov.l   .Lccn,r1
        mov.w   .L3f0,r0
        mov.l   r0,@(0x20,r1)
        .word   0xFFFD
        bra     _xr_resume
        mov     #0,r0
_xr_expevt_slot:
        mov.l   .Lccn,r1
        mov.w   .L555,r0
        bra     1f
        mov.l   r0,@(0x24,r1)
        nop
1:      trapa   #0x33
        bra     _xr_resume
        mov     #0,r0
_xr_expevt_adrerr:
        mov.l   .Lccn,r1
        mov.l   .Lmis,r2
        mov.w   .L666,r0
        mov.l   r0,@(0x24,r1)
        mov.l   @r2,r3
        bra     _xr_resume
        mov     #0,r0
_xr_trapa_slot:
        mov     #0,r0
_xr_slot_bra:
        bra     _xr_resume
        trapa   #0x44
_xr_resume:
        rts
        nop
        .balign 4
.Lccn:  .long   0xFF000000
.Lmis:  .long   0x8C0F0002
.L777:  .word   0x777
.L3fc:  .word   0x3FC
.L3f0:  .word   0x3F0
.L555:  .word   0x555
.L666:  .word   0x666

        .macro  XI n
        .balign 32
        .global _xr_intevt\n, _xr_intevt\n\()_st
_xr_intevt\n:
        mov.l   3f,r1
        mov.w   4f,r2
        ldc     r4,sr
        .rept   \n
        nop
        .endr
_xr_intevt\n\()_st:
        mov.l   r2,@(0x28,r1)
        .rept   16
        nop
        .endr
        rts
        mov.l   @(0x28,r1),r0
        .balign 4
3:      .long   0xFF000000
4:      .word   0x3E0
        .endm
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
        XI      \k
        .endr
        .data
        .balign 4
        .global _xi_tab, _xi_st_tab
_xi_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
        .long   _xr_intevt\k
        .endr
_xi_st_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
        .long   _xr_intevt\k\()_st
        .endr
