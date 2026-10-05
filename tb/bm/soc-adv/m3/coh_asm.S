/* coh_asm.S -- coh.c's helpers (tb/bm/soc-adv/m3, from agents/verify/M3/adv/src).
 *   void flush_oc_16k(uint32_t dummy_p1, uint32_t ccr)   from P2: read one longword of every line of the 16 KB at
 *        dummy_p1 (every operand-cache index with OIX = 0: every dirty line is written back), then CCR <- ccr with no
 *        store in between, 8 NOPs
 *   void sq_line(uint32_t sqaddr, const uint32_t *w)    8 longword stores into the store queue at sqaddr (32-byte
 *        aligned: bit 5 picks SQ0 / SQ1), then PREF @sqaddr (the burst) */
        .text
        .balign 32
        .global _flush_oc_16k
_flush_oc_16k:
        mov.l   .Lf_p2,r0
        jmp     @r0
        nop
        .balign 4
.Lf_p2: .long   _flush_body + 0x20000000
_flush_body:
        mov.w   .Lf_n,r1            /* 512 lines */
1:      mov.l   @r4,r0
        dt      r1
        bf/s    1b
        add     #32,r4
        mov.l   .Lf_ccr,r1
        mov.l   r5,@r1
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
.Lf_ccr: .long  0xFF00001C
.Lf_n:  .word   512

