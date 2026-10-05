/* crt0.S -- start-up of the bare-metal p4b probe (tb/bm/p4bus; WI-P4).
 *
 * Entered at 0x8C010000 from the SoC testbench's boot stub (SOC.md 7.3: SR 0x400000F0 = MD 1, RB 0, BL 0, IMASK 15;
 * caches and MMU off; the stub has already written PMCR1 = 0xC023).  It recreates the part of KOS's run-time state the
 * probe depends on, then runs main() and ends the simulation:
 *   - the stack at the top of the 16 MB RAM (0x8D000000: KOS 2.1 runs main() on arch_stack_16m, kernel/arch/dreamcast/
 *     kernel/stack.c; M3, WI-SOC: the old stack top, .bss end + 32 KB, put the busiest frames on the operand-cache
 *     indices of the last .bss variables -- c1_ctx / c1_n in c1 -- so every call missed twice on the real caches,
 *     an OC conflict the console's KOS layout does not have); .bss cleared;
 *   - PMCR1 = 0xC023: PMCTR1 counts CPU cycles (event 0x23), as KOS's perf_cntr_timer_enable() leaves it; p4b.c
 *     brackets every sequence with two reads of PMCTR1L (0xFF100008);
 *   - VBR = a fatal-exception table: an exception exits with 0x100 | EXPEVT >> 4 (an interrupt: 0x200 | INTEVT >> 4),
 *     so a run that goes wrong ends at once with a non-zero code instead of printing garbage;
 *   - main()'s return value to the sim-control exit port 0xB8000000 (board.md 3.10), then SLEEP.
 */
        .section .text.start,"ax"
        .global _start
_start:
        mov.l   .Lstack, r15
        mov.l   .Lbss0, r0
        mov.l   .Lbss1, r1
        mov     #0, r2
1:      cmp/hs  r1, r0
        bt      2f
        mov.l   r2, @r0
        bra     1b
        add     #4, r0
2:      mov.l   .Lvbr, r0
        ldc     r0, vbr
        mov.l   .Lpmcr1, r1
        mov.l   .Lpmval, r0
        mov.w   r0, @r1
        mov     #0, r4
        mov     #0, r5
        mov.l   .Lmain, r0
        jsr     @r0
        nop
        mov.l   .Lexit, r1
        mov.l   r0, @r1
3:      sleep
        bra     3b
        nop
        .balign 4
.Lstack:        .long   0x8D000000
.Lbss0:         .long   __bss_start
.Lbss1:         .long   __bss_end
.Lvbr:          .long   _bm_vbr
.Lpmcr1:        .long   0xFF000084
.Lpmval:        .long   0x0000C023
.Lmain:         .long   _main
.Lexit:         .long   0xB8000000

        .section .text.vbr,"ax"
        .balign 1024
_bm_vbr:
        .org    0x100
        mov.l   1f, r1
        mov.l   @r1, r0
        bra     _bm_fatal
        mov     #1, r2
        .balign 4
1:      .long   0xFF000024              /* EXPEVT */
        .org    0x400
        mov.l   1f, r1
        mov.l   @r1, r0
        bra     _bm_fatal
        mov     #1, r2
        .balign 4
1:      .long   0xFF000024
        .org    0x600
        mov.l   1f, r1
        mov.l   @r1, r0
        bra     _bm_fatal
        mov     #2, r2
        .balign 4
1:      .long   0xFF000028              /* INTEVT */
_bm_fatal:
        shlr2   r0
        shlr2   r0
        shll8   r2
        or      r2, r0
        mov.l   2f, r1
        mov.l   r0, @r1
3:      sleep
        bra     3b
        nop
        .balign 4
2:      .long   0xB8000000
