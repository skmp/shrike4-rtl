/* crt0.S -- start-up of the bare-metal ports of the exceptions-intc probes (tb/bm/exceptions-intc).
 *
 * Entered from the SoC testbench's boot stub at 0x8C010000 (rtl/v1/SOC.md 7.3: SR 0x400000F0 = MD, RB 0, BL 0,
 * IMASK 15; VBR 0x8C000000; caches and MMU off) or from any loader with the same state.  It recreates the part of the
 * KOS run-time environment the console programs depend on, then runs main() and ends the simulation:
 *   - the stack at the top of the 16 MB RAM (0x8D000000: KOS 2.1 runs main() on arch_stack_16m, kernel/arch/dreamcast/
 *     kernel/stack.c; M3, WI-SOC: the old stack top, .bss end + 32 KB, put the busiest frames on the operand-cache
 *     indices of the last .bss variables -- c1_ctx / c1_n in c1 -- so every call missed twice on the real caches,
 *     an OC conflict the console's KOS layout does not have); .bss cleared;
 *   - FPSCR = 0x00040000 (KOS runs C code with DN = 1, round to nearest; the boot stub leaves 0x00040001);
 *   - PMCR1 = 0xC023: PMCTR1 (0xFF100008, "PMCTR0L" in the probes) counts CPU cycles, as under KOS [SOC.md 7.3];
 *   - Holly's interrupt routing as KOS left it on the console (excint2's "I holly" line: IML2NRM 0x00001008 -- vblank-in
 *     and the maple DMA end at level 2 --, IML4NRM 0x0007C000 and IML4ERR 0x00003000 at level 4, nothing at level 6)
 *     and everything pending acknowledged, as KOS's asic_init does (board.md 4.1).  Every probe outside excint2's I
 *     rows runs at IMASK 5 or above, where level 2 / 4 are never accepted; the shim's irq_disable / irq_restore stand
 *     in for KOS's service of those events (shim/kos.h).  M4 (WI-SOC): the excint2 I rows need the board's IRL;
 *   - main()'s return value is written to the sim-control exit port 0xB8000000 (board.md 3.10), then SLEEP.
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
2:      mov.l   .Lfpscr, r0
        lds     r0, fpscr
        mov.l   .Lpmcr1, r1
        mov.l   .Lpmval, r0
        mov.w   r0, @r1
        mov.l   .Lholly, r1         /* Holly's interrupt controller (P2) */
        mov     #0, r0
        mov.l   r0, @(0x14,r1)      /* IML2EXT, IML2ERR, IML4EXT, IML6NRM / EXT / ERR = 0 */
        mov.l   r0, @(0x18,r1)
        mov.l   r0, @(0x24,r1)
        mov.l   r0, @(0x30,r1)
        mov.l   r0, @(0x34,r1)
        mov.l   r0, @(0x38,r1)
        mov.l   .Liml2n, r0
        mov.l   r0, @(0x10,r1)      /* IML2NRM */
        mov.l   .Liml4n, r0
        mov.l   r0, @(0x20,r1)      /* IML4NRM */
        mov.l   .Liml4e, r0
        mov.l   r0, @(0x28,r1)      /* IML4ERR */
        mov     #-1, r0
        mov.l   r0, @(0x00,r1)      /* ISTNRM: acknowledge everything pending */
        mov.l   r0, @(0x08,r1)      /* ISTERR */
        mov.l   @(0x00,r1), r0      /* (read back: the writes have landed) */
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
.Lfpscr:        .long   0x00040000
.Lpmcr1:        .long   0xFF000084
.Lpmval:        .long   0x0000C023
.Lholly:        .long   0xA05F6900
.Liml2n:        .long   0x00001008
.Liml4n:        .long   0x0007C000
.Liml4e:        .long   0x00003000
.Lmain:         .long   _main
.Lexit:         .long   0xB8000000
