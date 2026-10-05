/* p4rules_asm.S -- quadword FMOVs to / from the P4 bridge, and misaligned P4 accesses (p4rules.c).
 *   void qwr(uint32_t addr, uint32_t hi, uint32_t lo)    FR0 <- hi, FR1 <- lo, SZ = 1, FMOV DR0,@addr, SZ = 0
 *   void qrd(uint32_t addr, uint32_t *out)               SZ = 1, FMOV @addr,DR2, SZ = 0; out[0] = FR2, out[1] = FR3
 *   uint32_t mis_l(uint32_t addr) / mis_w(uint32_t addr) a longword / word load at addr (misaligned: the handler
 *                                                        resumes at _mis_ret with r0 unchanged = 0xDEAD)
 *   void mis_sl(uint32_t addr)                           a longword store at addr */
        .text
        .balign 32
        .global _qwr, _qrd, _mis_l, _mis_w, _mis_sl, _mis_ret
_qwr:
        sts     fpscr,r7
        mov.l   r5,@-r15
        fmov.s  @r15+,fr0
        mov.l   r6,@-r15
        fmov.s  @r15+,fr1
        mov.l   .Lsz,r0
        or      r7,r0
        lds     r0,fpscr
        fmov    dr0,@r4
        rts
        lds     r7,fpscr
_qrd:
        sts     fpscr,r7
        mov.l   .Lsz,r0
        or      r7,r0
        lds     r0,fpscr
        fmov    @r4,dr2
        lds     r7,fpscr
        fmov.s  fr2,@r5
        add     #4,r5
        rts
        fmov.s  fr3,@r5
_mis_l:
        mov.w   .Ldead,r0
        mov.l   @r4,r0
        bra     _mis_ret
        nop
_mis_w:
        mov.w   .Ldead,r0
        mov.w   @r4,r0
        bra     _mis_ret
        nop
_mis_sl:
        mov.w   .Ldead,r0
        mov.l   r0,@r4
_mis_ret:
        rts
        nop
        .balign 4
.Lsz:   .long   0x00100000
.Ldead: .word   0x7EAD

/* void nodrv_seq(uint32_t *ram, uint32_t *out): every probe is "an operand read, then a read of the undriven UBC window
 * 0xFF200100" with the window address already in r6 and the Pclk address in r7 (no literal load in between).
 * ram[0] = DEADBEEF, ram[1] = 00008001, ram[2] = 000000F0, ram[3] = 12345678; TCOR0 = 0BADC0DE (set by the caller)
 *   out[0] after mov.l @ram            out[1] after mov.w @(ram+4) (0x8001: sign-extended in the register)
 *   out[2] after mov.b @(ram+8) (0xF0)  out[3] after a Pclk read of TCOR0
 *   out[4] after mov.l @(ram+12) and then a store of 0x11111111   out[5] a 16-bit window read after a window read
 *   out[6] after mov.l @(ram+12) and then PREF @ram (a read-type access with no data)
 *   out[7] after mov.l @(ram+12) and then STC SR (no operand access) */
        .balign 32
        .global _nodrv_seq
_nodrv_seq:
        mov.l   r8,@-r15
        mov.l   .Lnodrv,r6
        mov.l   .Ltcor0,r7
        mov.l   .L1111,r8
        mov.l   @r4,r0
        mov.l   @r6,r1
        mov.l   r1,@(0,r5)
        mov.w   @(4,r4),r0
        mov.l   @r6,r1
        mov.l   r1,@(4,r5)
        mov.b   @(8,r4),r0
        mov.l   @r6,r1
        mov.l   r1,@(8,r5)
        mov.l   @r7,r0
        mov.l   @r6,r1
        mov.l   r1,@(12,r5)
        mov.l   @(12,r4),r0
        mov.l   r8,@r4
        mov.l   @r6,r1
        mov.l   r1,@(16,r5)
        mov.l   @r6,r1
        mov.w   @r6,r1
        extu.w  r1,r1
        mov.l   r1,@(20,r5)
        mov.l   @(12,r4),r0
        pref    @r4
        mov.l   @r6,r1
        mov.l   r1,@(24,r5)
        mov.l   @(12,r4),r0
        stc     sr,r2
        mov.l   @r6,r1
        mov.l   r1,@(28,r5)
        rts
        mov.l   @r15+,r8
        .balign 4
.Lnodrv: .long  0xFF200100
.Ltcor0: .long  0xFFD80008
.L1111:  .long  0x11111111
