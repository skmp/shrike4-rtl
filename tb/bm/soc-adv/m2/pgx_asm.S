/* pgx_asm.S -- page-boundary templates for pgx.c.  Each case is two pieces: <c>_a (copied so that it ends exactly at
 * the end of page A) and <c>_b (copied to offset 0 of page B = A + 4K, which may be unmapped).  The fetch runs ahead
 * from A into B (a speculative ITLB refill, or a UTLB-miss poison) while the instruction at A's end does its thing.
 * Entry: the caller JSRs to the first instruction of <c>_a with r4 / r5 / r6 = arguments, r0 = 0.  Every case returns
 * with RTS (PR = the caller); a faulting case resumes at g_resume = _pgx_ret (returns r0 = 0x0BAD).
 *   trapa   ... TRAPA #0x21 | B: rts ; add #1,r0             TRAPA at A's last word; returns into B
 *   ill     ... .word 0xFFFD | (B unmapped)                    illegal instruction at A's last word
 *   slp     ... SLEEP | B: rts ; add #1,r0                     SLEEP at A's last word (a TMU0 interrupt wakes it)
 *   p4rd    mov.l @r4,r1 (a Pclk register) ; nop x2 ; rts | B: add #1,r0    RTS's slot in B, a P4 read's MA hold
 *   ldcsr   ... ldc r5,sr | B: rts ; add #1,r0                 LDC SR's refetch into B
 *   dmiss   ... mov.l @r4,r0 (unmapped data) ; nop | B: rts ; nop    a data TLB miss while the run-ahead refills B
 *   cross   32 ALU instructions, 16 in A, 16 in B, then rts     a demand ITLB refill on straight-line code
 *   brau    bra to C (unmapped) ; nop                          an instruction TLB miss at a branch target
 *   braslot ... bra <anywhere> at A's last word | B unmapped: the slot    the delay slot's TLB miss (SPC = the bra)
 *   pgx_ret: rts ; mov #0x0BAD... (the recovery label) */
        .text
        .global _trapa_a, _trapa_a_e, _trapa_b, _trapa_b_e
        .balign 4
_trapa_a:
        nop
        nop
        trapa   #0x21
_trapa_a_e:
_trapa_b:
        rts
        add     #1,r0
_trapa_b_e:

        .global _ill_a, _ill_a_e
        .balign 4
_ill_a:
        nop
        nop
        .word   0xFFFD
_ill_a_e:

        .global _slp_a, _slp_a_e, _slp_b, _slp_b_e
        .balign 4
_slp_a:
        nop
        nop
        sleep
_slp_a_e:
_slp_b:
        rts
        add     #1,r0
_slp_b_e:

        .global _p4rd_a, _p4rd_a_e, _p4rd_b, _p4rd_b_e
        .balign 4
_p4rd_a:
        mov.l   @r4,r1
        nop
        nop
        rts
_p4rd_a_e:
_p4rd_b:
        add     #1,r0
_p4rd_b_e:

        .global _ldcsr_a, _ldcsr_a_e, _ldcsr_b, _ldcsr_b_e
        .balign 4
_ldcsr_a:
        nop
        nop
        ldc     r5,sr
_ldcsr_a_e:
_ldcsr_b:
        rts
        add     #1,r0
_ldcsr_b_e:

        .global _dmiss_a, _dmiss_a_e, _dmiss_b, _dmiss_b_e
        .balign 4
_dmiss_a:
        nop
        mov.l   @r4,r0
        nop
_dmiss_a_e:
_dmiss_b:
        rts
        nop
_dmiss_b_e:

        .global _cross_a, _cross_a_e, _cross_b, _cross_b_e
        .balign 4
_cross_a:
        add     r4,r0
        shll    r0
        add     r5,r0
        xor     r4,r0
        add     #3,r0
        shlr    r0
        add     r6,r0
        not     r0,r1
        and     r4,r1
        or      r1,r0
        add     r4,r0
        shll    r0
        add     r5,r0
        xor     r6,r0
        add     #5,r0
        dt      r1
_cross_a_e:
_cross_b:
        add     r1,r0
        shll    r0
        add     r5,r0
        xor     r4,r0
        add     #7,r0
        shlr    r0
        add     r6,r0
        not     r0,r1
        and     r5,r1
        or      r1,r0
        add     r4,r0
        shll2   r0
        add     r5,r0
        xor     r6,r0
        rts
        add     #9,r0
_cross_b_e:

        .global _brau_a, _brau_a_e
        .balign 4
_brau_a:
        nop
        jmp     @r4
        nop
_brau_a_e:

        .global _braslot_a, _braslot_a_e
        .balign 4
_braslot_a:
        nop
        nop
        jmp     @r4
_braslot_a_e:

        .balign 32
        .global _pgx_ret
_pgx_ret:
        mov.w   1f,r0
        rts
        nop
1:      .word   0x0BAD

/* void pgx_ici(void) (M3-fix, M3-verify F10): CCR.ICI from P2 (hm 4.2.1: CCR is written from P2), CCR otherwise
 * unchanged, then 8 instructions before the return to P1: the code pgx copied with P1 stores (and wrote back with
 * OCBWB) is fetched from memory, not through an I-cache line of an earlier run (the real I-cache, rm / rmx) */
        .balign 32
        .global _pgx_ici
_pgx_ici:
        mov.l   .Lpi_p2,r0
        jmp     @r0
        nop
        .balign 4
.Lpi_p2: .long  .Lpi_body + 0x20000000
.Lpi_body:
        mov.l   .Lpi_ccr,r1
        mov.l   @r1,r0
        mov.l   .Lpi_ici,r2
        or      r2,r0
        mov.l   r0,@r1
        mov.l   @r1,r0
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        rts
        nop
        .balign 4
.Lpi_ccr: .long 0xFF00001C
.Lpi_ici: .long 0x00000800
