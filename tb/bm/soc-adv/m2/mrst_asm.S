/* mrst_asm.S -- helpers of mrst.c.
 *   (call_va is m2util.S's)
 *   uint32_t ld_bl1(uint32_t va)    SR.BL <- 1 (MD 1, IMASK 15), then r0 <- @va: a fault here is the CPU-only reset */
        .text
        .balign 32
        .global _ld_bl1
_ld_bl1:
        mov.l   .Lsrbl,r1
        ldc     r1,sr
        nop
        mov.l   @r4,r0
        nop
        rts
        nop
        .balign 4
.Lsrbl: .long   0x500000F0
/*   void st_ill_bl1(uint32_t *reg, uint32_t val, uint32_t *reg2, uint32_t val2)   SR.BL <- 1; @reg2 <- val2 (PTEH);
 *   @reg <- val (MMUCR); an illegal instruction right behind the stores: the CPU-only reset with both stores in flight */
        .balign 32
        .global _st_ill_bl1
_st_ill_bl1:
        mov.l   .Lsrbl2,r1
        ldc     r1,sr
        nop
        mov.l   r7,@r6
        mov.l   r5,@r4
        .word   0xFFFD
        rts
        nop
        .balign 4
.Lsrbl2: .long  0x500000F0
