/* cpureset_asm.S -- the CPU-only reset sequences of cpureset.c (hm 5.5.3: a general exception with SR.BL = 1 resets
 * the CPU's registers, the other modules keep their contents, and the CPU branches to 0xA0000000).
 *   void go_illegal(uint32_t addr, uint32_t val16, uint32_t bl_sr)   R0-R7 of both banks <- patterns; SR <- bl_sr;
 *                                                                    a posted 16-bit store (val16 to addr); then an
 *                                                                    illegal instruction (0xFFFD)
 *   void go_trapa(uint32_t addr, uint32_t val16, uint32_t bl_sr)     the same with TRAPA #0x21
 *   void go_adrerr(uint32_t addr, uint32_t val32, uint32_t bl_sr)    a posted 32-bit store (a Pclk longword lands at
 *                                                                    strobe + 12), then a misaligned load
 * None returns: the reset re-runs the boot stub, which jumps to _start again. */
        .text
        .macro  PATTERNS
        mov.l   .Lb1_0,r0
        ldc     r0,r0_bank
        mov.l   .Lb1_1,r0
        ldc     r0,r1_bank
        mov.l   .Lb1_2,r0
        ldc     r0,r2_bank
        mov.l   .Lb1_3,r0
        ldc     r0,r3_bank
        mov.l   .Lb1_4,r0
        ldc     r0,r4_bank
        mov.l   .Lb1_5,r0
        ldc     r0,r5_bank
        mov.l   .Lb1_6,r0
        ldc     r0,r6_bank
        mov.l   .Lb1_7,r0
        ldc     r0,r7_bank
        mov.l   .Lb0_0,r0
        mov.l   .Lb0_1,r1
        mov.l   .Lb0_2,r2
        mov.l   .Lb0_3,r3
        .endm

        .balign 32
        .global _go_illegal
_go_illegal:
        mov     r4,r9
        mov     r5,r10
        mov     r6,r11
        PATTERNS
        mov.l   .Lb0_4,r4
        mov.l   .Lb0_5,r5
        mov.l   .Lb0_6,r6
        mov.l   .Lb0_7,r7
        ldc     r11,sr              /* BL = 1 */
        mov.w   r10,@r9             /* posted Pclk store */
        .word   0xFFFD              /* illegal: a general exception with BL = 1 -> CPU-only reset */
        nop
        nop
        bra     .
        nop

        .balign 32
        .global _go_trapa
_go_trapa:
        mov     r4,r9
        mov     r5,r10
        mov     r6,r11
        PATTERNS
        mov.l   .Lb0_4,r4
        mov.l   .Lb0_5,r5
        mov.l   .Lb0_6,r6
        mov.l   .Lb0_7,r7
        ldc     r11,sr
        mov.w   r10,@r9
        trapa   #0x21
        nop
        bra     .
        nop

        .balign 32
        .global _go_adrerr
_go_adrerr:
        mov     r4,r9
        mov     r5,r10
        mov     r6,r11
        mov.l   .Lmis,r12
        PATTERNS
        mov.l   .Lb0_4,r4
        mov.l   .Lb0_5,r5
        mov.l   .Lb0_6,r6
        mov.l   .Lb0_7,r7
        ldc     r11,sr
        mov.l   r10,@r9             /* posted Pclk longword store */
        mov.l   @r12,r13            /* misaligned: data address error with BL = 1 */
        nop
        bra     .
        nop

        .balign 4
.Lb1_0: .long   0xB1000000
.Lb1_1: .long   0xB1111111
.Lb1_2: .long   0xB1222222
.Lb1_3: .long   0xB1333333
.Lb1_4: .long   0xB1444444
.Lb1_5: .long   0xB1555555
.Lb1_6: .long   0xB1666666
.Lb1_7: .long   0xB1777777
.Lb0_0: .long   0xB0000000
.Lb0_1: .long   0xB0111111
.Lb0_2: .long   0xB0222222
.Lb0_3: .long   0xB0333333
.Lb0_4: .long   0xB0444444
.Lb0_5: .long   0xB0555555
.Lb0_6: .long   0xB0666666
.Lb0_7: .long   0xB0777777
.Lmis:  .long   0x8C0F0002

        .global _get_sgr, _get_dbr
_get_sgr:
        rts
        stc     sgr,r0
_get_dbr:
        rts
        stc     dbr,r0
        .global _do_trapa33, _do_misaligned
_do_trapa33:
        trapa   #0x33
        rts
        nop
_do_misaligned:
        mov.l   .Lmis1,r1
        mov.l   @r1,r0              /* data address error (BL = 0): the handler resumes at g_resume */
        .global _do_misaligned_ret
_do_misaligned_ret:
        rts
        nop
        .balign 4
.Lmis1: .long   0x8C0F0001
