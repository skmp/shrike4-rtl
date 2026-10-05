/* m3rt.S -- run-time of the M3 verification's adversarial programs (tb/bm/soc-adv/m3, from agents/verify/M3/adv/src; report
 * agents/reports/integration/M3-verify.md).  Adapted from agents/verify/M2/adv/src/m2rt.S (the M2 verification's).
 * The programs run on rm (IDEAL_MEM = 0: the real IC / OC / SQ / BIU / BSC / DMAC) behind the boot stub with
 * --ccr 0x090D (KOS's CCR: ICE, OCE, CB) and --lockstep.
 *
 * Start-up at 0x8C010000 (the SoC testbench's boot stub jumps here, rtl/v1/SOC.md 7.3), in this order:
 *   1. the entry state is captured into g_entry (P2: uncached, so a CPU-only reset's re-run of the stub, whose CCR
 *      write invalidates the operand cache, cannot lose it): R0-R7 bank 0, R0-R7 bank 1, SR, GBR, VBR, SSR, SPC, SGR,
 *      DBR, PR, MACH, MACL, FPUL, FPSCR, R8-R14, R15;
 *   2. FPSCR 0x00040000, PMCR1 0xC023 (elapsed cycles), VBR = _m3vbr (P1: the vectors take I-cache misses);
 *   3. .bss cleared;
 *   4. main(); its return value to the sim-control exit port.
 * Vectors (0x100, 0x400, 0x600), bank-1 R0-R7 only, SR.BL = 1 throughout:
 *   - every exception appends a 48-byte record {kind, EXPEVT, INTEVT, TRA, TEA, SPC, SSR, 0, 0, PMCTR1L, PR, g_phase}
 *     to g_log (at most 48, then no more records) and then: TRAPA (0x160) RTEs; with g_skip != 0 any other exception
 *     resumes at SPC + 2 (the faulting instruction skipped: straight code only); else it resumes at g_resume with
 *     SR = g_resume_sr (g_resume = 0: exit 0x100 | EXPEVT >> 4);
 *   - interrupts: TMU0-2 (0x400 / 0x420 / 0x440) clear UNF (and UNIE with g_clr_unie), read back; with g_tmu_rand the
 *     TMU0 constant is reprogrammed from an LCG to 6..21 so the next underflows land at every phase; DMTE0-3 (0x640 ..
 *     0x6A0) clear their channel's CHCR.IE (TE stays for the program) and count in g_ndmte[ch]; every interrupt
 *     counts in g_nirq and is logged unless g_irq_quiet; any other INTEVT resumes at g_resume with IMASK 15 (exit
 *     0x2FF when g_resume = 0). */
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
        mov.l   r1,@r0              /* r15 at entry */
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
.Lent:          .long   0xAC7F0000          /* g_entry: P2, fixed (m3.h G_ENTRY) */
.Lstack:        .long   __stack_top
.Lbss0:         .long   __bss_start
.Lbss1:         .long   __bss_end
.Lfpscr:        .long   0x00040000
.Lvbr:          .long   _m3vbr
.Lpmcr1:        .long   0xFF000084
.Lpmval:        .long   0x0000C023
.Lmain:         .long   _main
.Lexit:         .long   0xB8000000

/* ------------------------------------------------------------------ run-time data (.bss is cleared at start) */
        .data
        .balign 4
        .global _g_log, _g_nlog, _g_resume, _g_resume_sr, _g_clr_unie, _g_phase, _g_skip
        .global _g_nirq, _g_irq_quiet, _g_tmu_rand, _g_seed, _g_nexc, _g_ndmte
_g_nlog:        .long   0
_g_resume:      .long   0
_g_resume_sr:   .long   0
_g_clr_unie:    .long   0
_g_phase:       .long   0
_g_skip:        .long   0
_g_nirq:        .long   0
_g_irq_quiet:   .long   0
_g_tmu_rand:    .long   0
_g_seed:        .long   12345
_g_nexc:        .long   0
_g_ndmte:       .long   0, 0, 0, 0
        .balign 32
_g_log:         .skip   48*48

/* ------------------------------------------------------------------ vectors */
        .section .text.vbr,"ax"
        .balign 1024
        .global _m3vbr
_m3vbr:
        .org    0x100
        mov.l   1f,r6
        mov.l   @r6,r6              /* PMCTR1L at entry */
        bra     _m3_common
        mov     #1,r7
        .balign 4
1:      .long   0xFF100008
        .org    0x400
        mov.l   1f,r6
        mov.l   @r6,r6
        bra     _m3_common
        mov     #2,r7
        .balign 4
1:      .long   0xFF100008
        .org    0x600
        mov.l   1f,r6
        mov.l   @r6,r6
        bra     _m3_common
        mov     #3,r7
        .balign 4
1:      .long   0xFF100008

_m3_common:
        mov.l   .Lh_nexc,r0
        mov.l   @r0,r1
        add     #1,r1
        mov.l   r1,@r0
        mov     r7,r0
        cmp/eq  #3,r0
        bf      .Lh_dolog
        mov.l   .Lh_quiet,r0
        mov.l   @r0,r0
        tst     r0,r0
        bf      .Lh_irq
.Lh_dolog:
        mov.l   .Lh_nlog,r0
        mov.l   @r0,r1
        mov     #48,r2
        cmp/hs  r2,r1
        bt      .Lh_nolog
        mov     r1,r3
        shll2   r3
        shll2   r3              /* * 16 */
        mov     r3,r2
        shll    r3              /* * 32 */
        add     r2,r3           /* * 48 */
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
        mov     #0,r0
        mov.l   r0,@(28,r3)
        mov.l   r0,@(32,r3)
        mov.l   r6,@(36,r3)
        sts     pr,r0
        mov.l   r0,@(40,r3)
        mov.l   .Lh_phase,r0
        mov.l   @r0,r0
        mov.l   r0,@(44,r3)
.Lh_nolog:
        mov     r7,r0
        cmp/eq  #3,r0
        bt      .Lh_irq
        /* exceptions */
        mov.l   .Lh_ccn,r1
        mov.l   @(0x24,r1),r0
        mov.w   .Lh_160,r2
        cmp/eq  r2,r0
        bt      .Lh_rte
        mov.l   .Lh_skip_p,r1
        mov.l   @r1,r0
        tst     r0,r0
        bt      .Lh_resume
        stc     spc,r0              /* g_skip: resume behind the faulting instruction */
        add     #2,r0
        ldc     r0,spc
        rte
        nop
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
.Lh_exit:
        mov.l   .Lh_simexit,r1
        mov.l   r0,@r1
7:      sleep
        bra     7b
        nop

.Lh_irq:
        mov.l   .Lh_nirq,r1
        mov.l   @r1,r0
        add     #1,r0
        mov.l   r0,@r1
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
        /* DMTE0-3: INTEVT 0x640 + 0x20 * ch */
        mov.w   .Lh_640,r2
        sub     r2,r0
        mov     #0x7F,r2
        cmp/hi  r2,r0               /* (unsigned) INTEVT - 0x640 > 0x7F: not a DMTE (0x640 .. 0x6A0) */
        bt      .Lh_other
        tst     #0x1f,r0
        bf      .Lh_other
        shlr2   r0
        shlr2   r0                  /* ch * 2 */
        shll    r0                  /* ch * 4 */
        mov.l   .Lh_ndmte,r1
        add     r0,r1
        mov.l   @r1,r2
        add     #1,r2
        mov.l   r2,@r1
        shll2   r0                  /* ch * 16 */
        mov.l   .Lh_chcr0,r1
        add     r0,r1
        mov.l   @r1,r0
        mov     #-5,r2              /* ~CH_IE */
        and     r2,r0
        mov.l   r0,@r1
        mov.l   @r1,r0              /* read-back: the level is down before RTE */
        rte
        nop
.Lh_other:
        mov.l   .Lh_resume_p,r1     /* unknown source: resume at g_resume with IMASK 15 */
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
        bt      8f
        mov.w   .Lh_nunie,r2        /* ~0x0020 */
        and     r2,r0
8:      mov.w   r0,@r1
        mov.w   @r1,r0              /* read-back: the flag is clear before RTE */
        mov.l   .Lh_rand_p,r3
        mov.l   @r3,r3
        tst     r3,r3
        bt      9f
        mov.l   .Lh_seed_p,r3       /* seed = seed * 1103515245 + 12345; TCOR0 = 6 + ((seed >> 16) & 15) */
        mov.l   @r3,r0
        mov.l   .Lh_lcga,r2
        sts     macl,r5             /* the interrupted code's MACL survives */
        mul.l   r2,r0
        sts     macl,r0
        lds     r5,macl
        mov.l   .Lh_lcgc,r2
        add     r2,r0
        mov.l   r0,@r3
        shlr16  r0
        mov     #15,r2
        and     r2,r0
        add     #6,r0
        mov.l   .Lh_tmu_base,r1
        mov.l   r0,@(8,r1)          /* TCOR0 */
9:      rte
        nop
        .balign 4
.Lh_nlog:       .long   _g_nlog
.Lh_log:        .long   _g_log
.Lh_ccn:        .long   0xFF000000
.Lh_resume_p:   .long   _g_resume
.Lh_resume_sr_p: .long  _g_resume_sr
.Lh_clr_unie_p: .long   _g_clr_unie
.Lh_skip_p:     .long   _g_skip
.Lh_simexit:    .long   0xB8000000
.Lh_tmu_base:   .long   0xFFD80000
.Lh_chcr0:      .long   0xFFA0000C
.Lh_ndmte:      .long   _g_ndmte
.Lh_sr15:       .long   0x400000F0
.Lh_nirq:       .long   _g_nirq
.Lh_nexc:       .long   _g_nexc
.Lh_quiet:      .long   _g_irq_quiet
.Lh_phase:      .long   _g_phase
.Lh_rand_p:     .long   _g_tmu_rand
.Lh_seed_p:     .long   _g_seed
.Lh_lcga:       .long   1103515245
.Lh_lcgc:       .long   12345
.Lh_160:        .word   0x160
.Lh_100:        .word   0x100
.Lh_2ff:        .word   0x2ff
.Lh_400:        .word   0x400
.Lh_420:        .word   0x420
.Lh_440:        .word   0x440
.Lh_640:        .word   0x640
.Lh_nunf:       .word   0xFEFF
.Lh_nunie:      .word   0xFFDF
