/* crt0.S -- bare-metal start of the bsc-memmap console programs on the v1 SoC testbench (WI-BSC).
 *
 * Entered from the direct-boot stub at 0x8C010000 (rtl/v1/SOC.md 7.3: SR 0x400000F0 = MD, IMASK 15; caches off; the
 * BSC, QACR0 / 1 = 4, PMCR1 already as board.md 5.3 leaves them).  It
 *   1. sets the stack where KOS's main starts (0x8D000000, KOS's arch_stack_16m, less arch_main's 108-byte frame:
 *      0x8CFFFF94; the stack lines' operand-cache indices are the console's, M3 WI-SOC),
 *   2. turns the caches on as KOS leaves them (CCR <- 0x090D through P2, then 8 instructions: reads back 0x105),
 *   3. points VBR at a table whose every entry ends the run: exit code 0x100 | EXPEVT >> 4 (INTEVT for interrupts) to
 *      the sim-control port (board.md 3.10) -- the programs mask interrupts and take no exception,
 *   4. clears .bss, calls main(0, 0) and ends the simulation with main's return value (sim-control exit, 0x18000000
 *      through P2).
 */
        .little
        .section .text.start, "ax"
        .global _start
_start:
        mov.l   .Lstack, r15
        mov.l   .Lccr_p2, r0
        jsr     @r0
        nop
        mov.l   .Lvbr, r0
        ldc     r0, vbr
        mov.l   .Lbss_s, r1
        mov.l   .Lbss_e, r2
        mov     #0, r0
1:      cmp/hs  r2, r1
        bt      2f
        mov.l   r0, @r1
        bra     1b
        add     #4, r1
2:      mov.l   .Lmain, r0
        mov     #0, r4
        jsr     @r0
        mov     #0, r5
        mov.l   .Lexit, r1
        mov.l   r0, @r1
3:      sleep
        bra     3b
        nop

/* CCR <- 0x090D (ICI ICE OCI CB OCE), executed through its P2 alias; 8 instructions follow the write (hm 4.3) */
        .align  2
_bm_ccr_set:
        mov.l   .Lccr, r1
        mov.l   .Lccr_v, r2
        mov.l   r2, @r1
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

/* any exception or interrupt: exit 0x100 | code >> 4 (r0 = EXPEVT or INTEVT, loaded by the vector entry) */
_bm_unexpected:
        shlr2   r0
        shlr2   r0
        mov.w   .L100, r1
        or      r1, r0
        mov.l   .Lexit, r1
        mov.l   r0, @r1
5:      sleep
        bra     5b
        nop

        .align  2
.Lstack:  .long 0x8CFFFF94
.Lccr_p2: .long _bm_ccr_set + 0x20000000
.Lccr:    .long 0xFF00001C
.Lccr_v:  .long 0x0000090D
.Lvbr:    .long _bm_vbr
.Lbss_s:  .long _bss_start
.Lbss_e:  .long _bss_end
.Lmain:   .long _main
.Lexit:   .long 0xB8000000
.L100:    .word 0x100

/* the vector table: VBR + 0x100 general exceptions, + 0x400 TLB misses (EXPEVT), + 0x600 interrupts (INTEVT); in its
 * own section, placed after the programs' code (bm.ld: the program starts at the console's 0x8C01022C) */
        .section .text.vbr, "ax"
        .align  10
_bm_vbr:
        .org    _bm_vbr + 0x100
        mov.l   .Lv_exp1, r1
        mov.l   @r1, r0
        mov.l   .Lv_u1, r1
        jmp     @r1
        nop
        .align  2
.Lv_exp1: .long 0xFF000024
.Lv_u1:   .long _bm_unexpected
        .org    _bm_vbr + 0x400
        mov.l   .Lv_exp4, r1
        mov.l   @r1, r0
        mov.l   .Lv_u4, r1
        jmp     @r1
        nop
        .align  2
.Lv_exp4: .long 0xFF000024
.Lv_u4:   .long _bm_unexpected
        .org    _bm_vbr + 0x600
        mov.l   .Lv_int6, r1
        mov.l   @r1, r0
        mov.l   .Lv_u6, r1
        jmp     @r1
        nop
        .align  2
.Lv_int6: .long 0xFF000028
.Lv_u6:   .long _bm_unexpected
