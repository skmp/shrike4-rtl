/* rstx_asm.S -- rstx.c's triggers (tb/bm/soc-adv/m3, from agents/verify/M3/adv/src).  Each trigger is alone in one 32-byte line (one IC line:
 * a dry run with r4 = 0 warms it, so the real run's op and the H'FFFD behind it issue back to back from the I-cache):
 *   r4 = 0: return at once (the dry run);  r4 != 0: SR.BL <- 1, the op, H'FFFD (an illegal instruction with BL = 1:
 *   the CPU-only reset, SOC.md 2.2 / 4.3.7 -- PC 0xA0000000, the boot stub runs again and jumps to 0x8C010000).
 *   t_pref(a)          PREF @r5             (a store-queue burst, or an OC fill)
 *   t_st(a, v)         MOV.L r6,@r5         (a P2 store: the WTB; an SQ store; a P1 store miss)
 *   t_ocbp(a)          OCBP @r5
 *   t_movca(a, v)      MOVCA.L R0,@r5
 *   t_tas(a)           TAS.B @r5
 *   t_ill()            nothing (a DMA transfer started by the caller runs across the reset)
 *   t_jsr(fn)          JSR @r5 ; NOP        (a cold P1 routine: its IC fill in flight)
 *   t_evict(x, v, y)   MOV.L r6,@r5 ; MOV.L @r7,r0   (x's write-back in the WBB) */
        .macro TRIG name
        .text
        .balign 32
        .global _\name
_\name:
        tst     r4,r4
        bt      9f
        stc     sr,r0
        mov     #0x10,r1
        shll16  r1
        shll8   r1                  /* 0x10000000: BL */
        or      r1,r0
        ldc     r0,sr
        .endm
        .macro TEND
        .word   0xFFFD
9:      rts
        nop
        .endm

        TRIG    t_pref
        pref    @r5
        TEND
        TRIG    t_st
        mov.l   r6,@r5
        TEND
        TRIG    t_ocbp
        ocbp    @r5
        TEND
        TRIG    t_movca
        mov     r6,r0
        movca.l r0,@r5
        TEND
        TRIG    t_tas
        tas.b   @r5
        TEND
        TRIG    t_ill
        nop
        TEND
        TRIG    t_jsr
        jsr     @r5
        nop
        TEND
        TRIG    t_evict
        mov.l   r6,@r5
        mov.l   @r7,r0
        TEND

/* a cold routine for t_jsr: alone in its own line */
        .section .text.rtn,"ax"
        .balign 1024
        .global _r_cold
_r_cold:
        rts
        mov     #7,r0
        .balign 32

/* x_ld32_p2(a): one longword load from P2 code (the IC address array) */
        .text
        .balign 32
        .global _x_ld32_p2
_x_ld32_p2:
        mov.l   1f,r0
        jmp     @r0
        nop
        .balign 4
1:      .long   2f + 0x20000000
2:      rts
        mov.l   @r4,r0
