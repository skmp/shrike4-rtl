/* frq_asm.S -- a FRQCR write that starts a PLL relock (v1_cpg SETTLE), with a read of FRQCR right behind it (frq.c).
 *   void frq_seq<k>(uint32_t *out)   FRQCR <- 0x0A0A (PLL1 off: no relock), read back; FRQCR <- 0x0E0A (PLL1 0 -> 1:
 *                                    two WDT-counted relock phases, hm 10.5), k NOPs, read FRQCR twice.
 *                                    out[0..2] = the three reads */
        .text
        .macro  FS n
        .balign 32
        .global _frq_seq\n
_frq_seq\n:
        mov.l   1f,r1
        mov.w   2f,r0
        mov.w   r0,@r1
        mov.w   @r1,r2
        mov.w   3f,r0
        mov.w   r0,@r1
        .rept   \n
        nop
        .endr
        mov.w   @r1,r3
        mov.w   @r1,r5
        mov.l   r2,@r4
        mov.l   r3,@(4,r4)
        rts
        mov.l   r5,@(8,r4)
        .balign 4
1:      .long   0xFFC00000
2:      .word   0x0A0A
3:      .word   0x0E0A
        .endm
        .irp    k,0,1,2,3,4,5,6,7,8
        FS      \k
        .endr
        .data
        .balign 4
        .global _frq_tab
_frq_tab:
        .irp    k,0,1,2,3,4,5,6,7,8
        .long   _frq_seq\k
        .endr
