/* m3util.S -- helpers shared by the M3 verification's programs (tb/bm/soc-adv/m3, from agents/verify/M3/adv/src).
 *   void ccr_set(uint32_t v)   CCR <- v from P2 (hm 4.2.1: CCR must be written by a program in P2), read back, then 8
 *                              NOPs in P2 before returning (hm: an instruction fetch to P0 / P1 / P3 at least 8
 *                              instructions after the CCR write)
 *   uint32_t ccr_get(void)
 *   void p2_call(void (*f)(void))   calls f through its P2 alias (the routine must be position independent: PC-relative
 *                              literals and branches only)
 *   void sq_line(uint32_t sqaddr, const uint32_t *w)   8 longword stores into the store queue at sqaddr (32-byte
 *                              aligned: bit 5 picks SQ0 / SQ1), then PREF @sqaddr (the burst) */
        .text
        .balign 32
        .global _ccr_set, _ccr_get, _p2_call, _sq_line
_ccr_set:
        mov.l   .Lcs_p2,r0
        jmp     @r0
        nop
        .balign 4
.Lcs_p2: .long  _ccr_set_body + 0x20000000
_ccr_set_body:
        mov.l   .Lccr,r1
        mov.l   r4,@r1
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
.Lccr:  .long   0xFF00001C

        .balign 32
_ccr_get:
        mov.l   .Lccr2,r1
        rts
        mov.l   @r1,r0
        .balign 4
.Lccr2: .long   0xFF00001C

        .balign 32
_p2_call:
        sts.l   pr,@-r15
        mov.l   .Lmask,r0
        and     r0,r4
        mov.l   .Lp2,r0
        or      r0,r4
        jsr     @r4
        nop
        lds.l   @r15+,pr
        rts
        nop
        .balign 4
.Lmask: .long   0x1FFFFFFF
.Lp2:   .long   0xA0000000

        .balign 32
_sq_line:
        mov.l   @r5+,r0
        mov.l   r0,@(0,r4)
        mov.l   @r5+,r0
        mov.l   r0,@(4,r4)
        mov.l   @r5+,r0
        mov.l   r0,@(8,r4)
        mov.l   @r5+,r0
        mov.l   r0,@(12,r4)
        mov.l   @r5+,r0
        mov.l   r0,@(16,r4)
        mov.l   @r5+,r0
        mov.l   r0,@(20,r4)
        mov.l   @r5+,r0
        mov.l   r0,@(24,r4)
        mov.l   @r5+,r0
        mov.l   r0,@(28,r4)
        rts
        pref    @r4
