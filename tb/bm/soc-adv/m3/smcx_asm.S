/* smcx_asm.S -- smcx.c's sequences (tb/bm/soc-adv/m3, from agents/verify/M3/adv/src): self-modifying code, each sequence back to back from P2
 * (no cacheable fetch but the call of the patched routine):
 *   x_smc_p1(fn, word, k)   MOV.L word,@fn (P1: a dirty OC line) ; OCBWB @fn (the WBB) ; k ; CCR <- KOS | ICI ; JSR @fn
 *   x_smc_p2(fn, word, k)   MOV.L word,@(fn | P2) (the WTB) ; k ; CCR <- KOS | ICI ; JSR @fn
 *   x_smc_sq(fn, sqa, k)    PREF @sqa (the SQ holds the new line, stored by the caller) ; k ; ICI ; JSR @fn
 *   x_smc_wt(fn, word, k)   MOV.L word,@(fn & 0x1FFFFFFF) (P0, CCR.WT = 1: the WTB, the OC line updated if present) ;
 *                           k ; ICI ; JSR @fn
 * Each returns fn's r0.  r_smc*: MOV #1,R0 ; RTS ; NOP, each alone in its own 32-byte line, 1 KB apart. */
        .macro P2ENTRY name
        .text
        .balign 32
        .global _\name
_\name:
        mov.l   1f,r0
        jmp     @r0
        nop
        .balign 4
1:      .long   2f + 0x20000000
2:
        .endm
        .macro KDELAY
        tst     r6,r6
        bt      3f
4:      dt      r6
        bf      4b
3:
        .endm
        .macro ICI_CALL
        mov.l   5f,r1
        mov.l   6f,r0
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
        jsr     @r4
        nop
        lds.l   @r15+,pr
        rts
        nop
        .balign 4
5:      .long   0xFF00001C
6:      .long   0x00000907          /* ICE OCE CB WT | ICI */
        .endm

        P2ENTRY x_smc_p1
        sts.l   pr,@-r15
        mov.l   r5,@r4
        ocbwb   @r4
        KDELAY
        ICI_CALL

        P2ENTRY x_smc_p2
        sts.l   pr,@-r15
        mov.l   7f,r0
        and     r4,r0
        mov.l   8f,r1
        or      r1,r0
        mov.l   r5,@r0
        KDELAY
        ICI_CALL
        .balign 4
7:      .long   0x1FFFFFFF
8:      .long   0xA0000000

        P2ENTRY x_smc_sq
        sts.l   pr,@-r15
        pref    @r5
        KDELAY
        ICI_CALL

        P2ENTRY x_smc_wt
        sts.l   pr,@-r15
        mov.l   9f,r0
        and     r4,r0
        mov.l   r5,@r0
        KDELAY
        ICI_CALL
        .balign 4
9:      .long   0x1FFFFFFF

        .section .text.rtn,"ax"
        .balign 1024
        .global _r_smc0, _r_smc1, _r_smc2, _r_smc3
_r_smc0:
        mov     #1,r0
        rts
        nop
        .balign 1024
_r_smc1:
        mov     #1,r0
        rts
        nop
        .balign 1024
_r_smc2:
        mov     #1,r0
        rts
        nop
        .balign 1024
_r_smc3:
        mov     #1,r0
        rts
        nop
        .balign 32
