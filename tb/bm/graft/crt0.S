/* crt0.S -- start of a grafted console probe (tb/bm/graft, WI-SOC M3; README.md).
 *
 * The image is the console build's own ELF (KOS and the probe, byte for byte at the console's addresses) with KOS's
 * start and its console / allocator entry points redirected here (graft.py).  The boot stub (SOC.md 7.3) jumps to
 * 0x8C010000 = KOS's `start`, whose first bytes branch to _bm_start.  It recreates the part of KOS's start-up the
 * probes depend on (kernel/arch/dreamcast/kernel/startup.S, init.c arch_main), then runs the console's main():
 *   - r15 = 0x8CFFFF94: KOS's arch_stack_16m (0x8D000000) less arch_main's 108-byte frame (seven registers, PR and
 *     add #-76: tools/hw/<suite>/<prog>.elf), so every probe frame sits on the console's operand-cache indices;
 *   - the console image's .bss (__bss_start .. _end) and the runtime's cleared;
 *   - FPSCR = 0x00040000 (startup.S: DN = 1);
 *   - PMCR1 = 0xC023 (PRFC0 counts CPU cycles: KOS's perf_cntr_timer_enable(), which the timer init runs);
 *   - VBR = a fatal table (any exception exits 0x100 | EXPEVT >> 4, an interrupt 0x200 | INTEVT >> 4), then, for a
 *     probe that installs KOS exception handlers (graft.py --irq), KOS's irq_init() steps (rt_graft.c
 *     bm_kos_irq_init: KOS's default handlers, IPRC.DMAC = 3, its default context, VBR = KOS's table);
 *   - SR.IMASK = 0 (KOS's arch_main ends its init with irq_enable(); the probes mask interrupts themselves), then
 *     main(0, 0) at its console address; its return value to the sim-control exit port 0xB8000000 (board.md 3.10).
 * Not recreated: KOS's TMU0 / TMU2 (the thread and ms timers, IPRA 0xF0F0) and their interrupts between repetitions,
 * the rest of KOS's init (maple, video, the file systems, dcload's console).
 */
        .section .text.start,"ax"
        .global _bm_start
_bm_start:
        mov.l   .Lstack, r15
        mov.l   .Lcb0, r4
        mov.l   .Lcb1, r5
        bsr     bm_zero
        nop
        mov.l   .Lrb0, r4
        mov.l   .Lrb1, r5
        bsr     bm_zero
        nop
        mov.l   .Lfpscr, r0
        lds     r0, fpscr
        mov.l   .Lvbr, r0
        ldc     r0, vbr
        mov.l   .Lpmcr1, r1
        mov.l   .Lpmval, r0
        mov.w   r0, @r1
        mov.l   .Lkirq, r0
        tst     r0, r0
        bt      1f
        mov.l   .Lirqinit, r0
        jsr     @r0
        nop
1:      stc     sr, r0                  /* SR.IMASK = 0: KOS's arch_main ends its init with irq_enable() */
        mov.l   .Limask, r1
        and     r1, r0
        ldc     r0, sr
        mov.l   .Lmain, r0
        mov     #0, r4
        jsr     @r0
        mov     #0, r5
        mov.l   .Lexit, r1
        mov.l   r0, @r1
3:      sleep
        bra     3b
        nop
/* zero [r4, r5) (both 4-aligned) */
bm_zero:
        cmp/hs  r5, r4
        bt      2f
        mov     #0, r0
        mov.l   r0, @r4
        bra     bm_zero
        add     #4, r4
2:      rts
        nop
        .balign 4
.Lstack:        .long   0x8CFFFF94
.Lcb0:          .long   _con_bss0
.Lcb1:          .long   _con_bss1
.Lrb0:          .long   _bm_bss0
.Lrb1:          .long   _bm_bss1
.Lfpscr:        .long   0x00040000
.Lvbr:          .long   _bm_vbr
.Lpmcr1:        .long   0xFF000084
.Lpmval:        .long   0x0000C023
.Lkirq:         .long   _bm_kos_irq
.Lirqinit:      .long   _bm_kos_irq_init
.Lmain:         .long   _con_main
.Limask:        .long   0xFFFFFF0F
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
