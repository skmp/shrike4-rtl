/* adv_rt.S -- run-time of the SoC adversarial bare-metal programs (tb/bm/soc-adv; from the M1a verification).
 *
 * Start-up at 0x8C010000 (the SoC testbench's boot stub jumps here, rtl/v1/SOC.md 7.3), in this order:
 *   1. the entry state is captured before anything touches it (g_entry: R0-R7 bank 0, R0-R7 bank 1, SR, GBR, VBR,
 *      SSR, SPC, SGR, DBR, PR, MACH, MACL, FPUL, FPSCR, R8-R14, R15): the CPU-only reset test reads it after the reset
 *      re-ran the boot stub (which leaves R0-R7 of both banks alone);
 *   2. KOS's run-time state (FPSCR 0x00040000, PMCR1 0xC023) and VBR = _advvbr;
 *   3. .bss cleared (.data is not reloaded after a CPU-only reset: g_* persistent flags live there);
 *   4. main(); its return value to the sim-control exit port.
 * Vectors: every exception / interrupt appends a 32-byte record {kind, EXPEVT, INTEVT, TRA, TEA, SPC, SSR, PMCTR1L}
 * to g_log (at most 48) and then:
 *   - an interrupt: the source named by INTEVT is cleared (TMU0-2 UNF, and UNIE too with g_clr_unie; WDT ITI: TME;
 *     REF RCMI: CMF / CMIE; SCIF TXI / SCI TXI: TIE) with a read-back (none with g_noreadback), then RTE to SPC;
 *     an unknown INTEVT resumes at g_resume with IMASK 15;
 *   - TRAPA (EXPEVT 0x160): RTE (SPC = the next instruction);
 *   - any other exception: resumes at g_resume with SSR = g_resume_sr (0: exit with 0x100 | EXPEVT >> 4).
 * The handler uses bank-1 R0-R7 only. */
        .section .text.start,"ax"
        .global _start
_start:
        mov.l   r7,@-r15
        mov.l   r6,@-r15
        mov.l   r5,@-r15
        mov.l   r4,@-r15
        mov.l   r3,@-r15
        mov.l   r2,@-r15
        mov.l   r1,@-r15
        mov.l   r0,@-r15
        mov.l   .Lent,r0
        mov     r15,r1
        mov     #8,r2
1:      mov.l   @r1+,r3
        mov.l   r3,@r0
        dt      r2
        bf/s    1b
        add     #4,r0
        stc     r0_bank,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     r1_bank,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     r2_bank,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     r3_bank,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     r4_bank,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     r5_bank,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     r6_bank,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     r7_bank,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     sr,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     gbr,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     vbr,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     ssr,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     spc,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     sgr,r3
        mov.l   r3,@r0
        add     #4,r0
        stc     dbr,r3
        mov.l   r3,@r0
        add     #4,r0
        sts     pr,r3
        mov.l   r3,@r0
        add     #4,r0
        sts     mach,r3
        mov.l   r3,@r0
        add     #4,r0
        sts     macl,r3
        mov.l   r3,@r0
        add     #4,r0
        sts     fpul,r3
        mov.l   r3,@r0
        add     #4,r0
        sts     fpscr,r3
        mov.l   r3,@r0
        add     #4,r0
        mov.l   r8,@r0
        add     #4,r0
        mov.l   r9,@r0
        add     #4,r0
        mov.l   r10,@r0
        add     #4,r0
        mov.l   r11,@r0
        add     #4,r0
        mov.l   r12,@r0
        add     #4,r0
        mov.l   r13,@r0
        add     #4,r0
        mov.l   r14,@r0
        add     #4,r0
        mov.l   r1,@r0              /* r15 at entry (r1 = r15 + 32 after the copy loop) */
        /* the run-time proper */
        mov.l   .Lstack,r15
        mov.l   .Lbss0,r0
        mov.l   .Lbss1,r1
        mov     #0,r2
2:      cmp/hs  r1,r0
        bt      3f
        mov.l   r2,@r0
        bra     2b
        add     #4,r0
3:      mov.l   .Lfpscr,r0
        lds     r0,fpscr
        mov.l   .Lvbr,r0
        ldc     r0,vbr
        mov.l   .Lpmcr1,r1
        mov.l   .Lpmval,r0
        mov.w   r0,@r1
        mov.l   .Lmain,r0
        jsr     @r0
        nop
        mov.l   .Lexit,r1
        mov.l   r0,@r1
4:      sleep
        bra     4b
        nop
        .balign 4
.Lent:          .long   _g_entry
.Lstack:        .long   __stack_top
.Lbss0:         .long   __bss_start
.Lbss1:         .long   __bss_end
.Lfpscr:        .long   0x00040000
.Lvbr:          .long   _advvbr
.Lpmcr1:        .long   0xFF000084
.Lpmval:        .long   0x0000C023
.Lmain:         .long   _main
.Lexit:         .long   0xB8000000

/* ------------------------------------------------------------------ persistent data (never cleared) */
        .data
        .balign 4
        .global _g_entry, _g_log, _g_nlog, _g_resume, _g_resume_sr, _g_clr_unie, _g_noreadback, _g_phase
        .global _g_scratch
_g_entry:       .skip   36*4
_g_nlog:        .long   0
_g_resume:      .long   0
_g_resume_sr:   .long   0
_g_clr_unie:    .long   0
_g_noreadback:  .long   0
_g_phase:       .long   0
_g_scratch:     .skip   64*4
        .balign 32
_g_log:         .skip   48*32

/* ------------------------------------------------------------------ vectors */
        .section .text.vbr,"ax"
        .balign 1024
        .global _advvbr
_advvbr:
        .org    0x100
        mov.l   1f,r6
        mov.l   @r6,r6              /* PMCTR1L at entry */
        bra     _adv_common
        mov     #1,r7
        .balign 4
1:      .long   0xFF100008
        .org    0x400
        mov.l   1f,r6
        mov.l   @r6,r6
        bra     _adv_common
        mov     #2,r7
        .balign 4
1:      .long   0xFF100008
        .org    0x600
        mov.l   1f,r6
        mov.l   @r6,r6
        bra     _adv_common
        mov     #3,r7
        .balign 4
1:      .long   0xFF100008

_adv_common:
        mov.l   .Lh_nlog,r0
        mov.l   @r0,r1
        mov     #48,r2
        cmp/hs  r2,r1
        bt      .Lh_full
        mov     r1,r3
        shll2   r3
        shll2   r3
        shll    r3                  /* * 32 */
        mov.l   .Lh_log,r2
        add     r2,r3
        add     #1,r1
        mov.l   r1,@r0
        mov.l   r7,@(0,r3)
        mov.l   .Lh_ccn,r1
        mov.l   @(0x24,r1),r0
        mov.l   r0,@(4,r3)          /* EXPEVT */
        mov.l   @(0x28,r1),r0
        mov.l   r0,@(8,r3)          /* INTEVT */
        mov.l   @(0x20,r1),r0
        mov.l   r0,@(12,r3)         /* TRA */
        mov.l   @(0x0c,r1),r0
        mov.l   r0,@(16,r3)         /* TEA */
        stc     spc,r0
        mov.l   r0,@(20,r3)
        stc     ssr,r0
        mov.l   r0,@(24,r3)
        mov.l   r6,@(28,r3)
        mov     r7,r0
        cmp/eq  #3,r0
        bt      .Lh_irq
        /* exceptions */
        mov.l   .Lh_ccn,r1
        mov.l   @(0x24,r1),r0
        mov.w   .Lh_160,r2
        cmp/eq  r2,r0
        bt      .Lh_rte
.Lh_resume:
        mov.l   .Lh_resume_p,r1
        mov.l   @r1,r0
        tst     r0,r0
        bt      .Lh_fatal
        ldc     r0,spc
        mov.l   .Lh_resume_sr_p,r1
        mov.l   @r1,r0
        ldc     r0,ssr
.Lh_rte:
        rte
        nop
.Lh_fatal:
        mov.l   .Lh_ccn,r1
        mov.l   @(0x24,r1),r0
        shlr2   r0
        shlr2   r0
        mov.w   .Lh_100,r2
        or      r2,r0
        bra     .Lh_exit
        nop
.Lh_full:
        mov.w   .Lh_3ff,r0
.Lh_exit:
        mov.l   .Lh_simexit,r1
        mov.l   r0,@r1
5:      sleep
        bra     5b
        nop

.Lh_irq:
        mov.l   .Lh_ccn,r1
        mov.l   @(0x28,r1),r0       /* INTEVT */
        mov.w   .Lh_400,r2
        cmp/eq  r2,r0
        bt/s    .Lh_tmu
        mov     #0x10,r4            /* TCR0 offset */
        mov.w   .Lh_420,r2
        cmp/eq  r2,r0
        bt/s    .Lh_tmu
        mov     #0x1c,r4
        mov.w   .Lh_440,r2
        cmp/eq  r2,r0
        bt/s    .Lh_tmu
        mov     #0x28,r4
        mov.w   .Lh_560,r2
        cmp/eq  r2,r0
        bt      .Lh_wdt
        mov.w   .Lh_580,r2
        cmp/eq  r2,r0
        bt      .Lh_ref
        mov.w   .Lh_760,r2
        cmp/eq  r2,r0
        bt      .Lh_scif
        mov.w   .Lh_520,r2
        cmp/eq  r2,r0
        bt      .Lh_sci
        mov.l   .Lh_resume_p,r1     /* unknown source: resume at g_resume with IMASK 15 (it stays pending) */
        mov.l   @r1,r0
        tst     r0,r0
        bt      .Lh_fatal2
        ldc     r0,spc
        mov.l   .Lh_sr15,r0
        ldc     r0,ssr
        rte
        nop
.Lh_fatal2:
        mov.w   .Lh_2ff,r0
        bra     .Lh_exit
        nop
.Lh_tmu:
        mov.l   .Lh_tmu_base,r1
        add     r4,r1
        mov.w   @r1,r0
        mov.w   .Lh_nunf,r2         /* ~0x0100 */
        and     r2,r0
        mov.l   .Lh_clr_unie_p,r3
        mov.l   @r3,r3
        tst     r3,r3
        bt      6f
        mov.w   .Lh_nunie,r2        /* ~0x0020 */
        and     r2,r0
6:      mov.w   r0,@r1
        bra     .Lh_readback
        nop
.Lh_wdt:
        mov.l   .Lh_wtcsr,r1
        mov.w   .Lh_a500,r0
        mov.w   r0,@r1              /* TME = 0, IOVF = 0 */
        bra     .Lh_readback
        nop
.Lh_ref:
        mov.l   .Lh_rtcsr,r1
        mov.w   @r1,r0
        mov     #0x3f,r2
        and     r2,r0               /* CMF = CMIE = 0 */
        mov.w   .Lh_a500,r2
        or      r2,r0
        mov.w   r0,@r1
        bra     .Lh_readback
        nop
.Lh_scif:
        mov.l   .Lh_scscr2,r1
        mov.w   @r1,r0
        mov.w   .Lh_ntie,r2
        and     r2,r0
        mov.w   r0,@r1
        bra     .Lh_readback
        nop
.Lh_sci:
        mov.l   .Lh_scscr1,r1
        mov.b   @r1,r0
        mov.w   .Lh_ntie,r2
        and     r2,r0
        mov.b   r0,@r1
        mov.b   @r1,r0
        bra     .Lh_done
        nop
.Lh_readback:
        mov.l   .Lh_norb_p,r3
        mov.l   @r3,r3
        tst     r3,r3
        bf      .Lh_done
        mov.w   @r1,r0
.Lh_done:
        rte
        nop
        .balign 4
.Lh_nlog:       .long   _g_nlog
.Lh_log:        .long   _g_log
.Lh_ccn:        .long   0xFF000000
.Lh_resume_p:   .long   _g_resume
.Lh_resume_sr_p: .long  _g_resume_sr
.Lh_clr_unie_p: .long   _g_clr_unie
.Lh_norb_p:     .long   _g_noreadback
.Lh_simexit:    .long   0xB8000000
.Lh_tmu_base:   .long   0xFFD80000
.Lh_wtcsr:      .long   0xFFC0000C
.Lh_rtcsr:      .long   0xFF80001C
.Lh_scscr2:     .long   0xFFE80008
.Lh_scscr1:     .long   0xFFE00008
.Lh_sr15:       .long   0x400000F0
.Lh_160:        .word   0x160
.Lh_100:        .word   0x100
.Lh_3ff:        .word   0x3ff
.Lh_2ff:        .word   0x2ff
.Lh_400:        .word   0x400
.Lh_420:        .word   0x420
.Lh_440:        .word   0x440
.Lh_560:        .word   0x560
.Lh_580:        .word   0x580
.Lh_760:        .word   0x760
.Lh_520:        .word   0x520
.Lh_nunf:       .word   0xFEFF
.Lh_nunie:      .word   0xFFDF
.Lh_a500:       .word   0xA500
.Lh_ntie:       .word   0xFF7F
