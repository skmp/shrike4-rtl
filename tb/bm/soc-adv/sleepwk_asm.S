/* sleepwk_asm.S -- SLEEP right behind a posted Pclk store (sleepwk.c).
 *   uint32_t sl_st8(addr, val, sr)    SR <- sr; PMCTR1L sampled; byte store val -> addr; SLEEP; returns the PMCTR1L
 *   uint32_t sl_st16(addr, val, sr)   the same with a 16-bit store                                     delta
 *   uint32_t sk<k>(sr)                k = 0..12: SR <- sr (lowers IMASK with a source pending), k NOPs, SLEEP
 * _sl_st8_slp / _sl_st16_slp / _sk<k>_slp label the SLEEPs. */
        .text
        .balign 32
        .global _sl_st8, _sl_st8_slp
_sl_st8:
        mov.l   .Lpm,r1
        ldc     r6,sr
        mov.l   @r1,r2
        mov.b   r5,@r4
_sl_st8_slp:
        sleep
        mov.l   @r1,r0
        rts
        sub     r2,r0

        .balign 32
        .global _sl_st16, _sl_st16_slp
_sl_st16:
        mov.l   .Lpm,r1
        ldc     r6,sr
        mov.l   @r1,r2
        mov.w   r5,@r4
_sl_st16_slp:
        sleep
        mov.l   @r1,r0
        rts
        sub     r2,r0
        .balign 4
.Lpm:   .long   0xFF100008

        .macro  SK n
        .balign 32
        .global _sk\n, _sk\n\()_slp
_sk\n:
        mov.l   2f,r1
        mov.l   @r1,r2
        ldc     r4,sr
        .rept   \n
        nop
        .endr
_sk\n\()_slp:
        sleep
        mov.l   @r1,r0
        rts
        sub     r2,r0
        .balign 4
2:      .long   0xFF100008
        .endm
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12
        SK      \k
        .endr

        .data
        .balign 4
        .global _sk_tab, _sk_slp_tab
_sk_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12
        .long   _sk\k
        .endr
_sk_slp_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12
        .long   _sk\k\()_slp
        .endr
