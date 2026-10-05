/* v1_sweep_drv.S -- the sweep driver of make v1-soc-sweep (rtl/v1/SOC.md 5; owner WI-SOC, M3).
 *
 * The pairwise timing sweep (model/timings2) on the whole chip: the console runner's own jit_enter / jit_exit and each
 * case's JIT loop run on v1_soc with IDEAL_MEM = 0 (the real I-cache, operand cache, BIU, BSC and SDRAM), warm, as the
 * console ran them.  The boot stub jumps here with CCR = 0x090D written from P2 (KOS's, ICE / OCE / CB; soc_tb --sweep
 * sets it) and SR = MD, IMASK 15.  tb/soc_tb.cpp --sweep loads the runner image (model/timings2/runner/t2run.elf), this
 * driver (at DRV = 0x8C0C0800) and a job table (at TAB = 0x8C200000) into the board's SDRAM before the power-on reset,
 * and reads the results back after the exit.
 *
 * Per job (one sweep point in one shape: the testbench builds the code block, the scratch image and the state block
 * exactly as runner/t2run.c's jit_build / pool_build / scratch_build / state_build do) the driver does what t2run.c's
 * measure() / run_once() do:
 *   measure:  the code block -> jit_code with P1 stores; dcache_flush_range (OCBWB per line) and icache_flush_range
 *             (from P2: OCBWB and an IC address-array write per line, as KOS 2.1's cache.s) of jit_code; the state
 *             block -> the runner's `state`; the scratch image -> the driver's image buffer (see below)
 *   for each event k (PMCR1 = event: counter 1, KOS's PRFC0; counter 2 = elapsed Icyc, KOS's PRFC1):
 *             before the first: run_once(N1) as the warm-up (its result dropped, as t2run's)
 *             run_once(N1), run_once(N2): (cycles, events) of each
 *   run_once: state[13] = iterations; memcpy(scratch, image, 512) with P1 stores; OCBWB of the scratch lines
 *             (dcache_flush_range); the timed window: PMCTR2L, PMCTR1L, jsr jit_enter(state), PMCTR2L, PMCTR1L
 * Two deliberate differences from t2run.c, both outside what N2 - N1 measures (they only remove noise the console's
 * min over REPS = 3 runs of 128 iterations removed):
 *   - The scratch image is copied from the driver's own buffer, not t2run's scratch_img: scratch_img (0x8C061300)
 *     shares an operand-cache index with jit_exit's literal pool (0x8C011314, index 0x098), so every timed run would
 *     miss once in jit_enter, at a cost that depends on the Bclk phase (14 / 15, SOC.md 3.3.1), and the two runs of a
 *     pair need not start at the same phase (the pair differs by N2 - N1 = 2 iterations of any period).
 *   - One Bclk-class P4 read (BCR2) right before the first PMCTR read: it completes on a Bclk edge, so every timed run
 *     starts at the same Bclk phase and a loop whose first iterations use the bus (a cache-block form) pays the same
 *     fixed cost in the N1 and the N2 run.
 * Nothing else is touched between the warm-up and the timed runs, and no line the timed window uses shares a cache
 * index with the driver or its data (tb/soc_tb.cpp checks the indices of every line against the runner's symbols):
 *   driver code and literals  IC / OC 0x040-0x05F     driver data (image, header copies, results)  OC 0x060-0x07F
 *
 * Job table (little endian words; TAB is P1):
 *   TAB + 0   'SWDR'   +4 njobs   +8 &jit_enter   +12 &jit_code   +16 &scratch   +20 &state   +24 result base (P2:
 *             uncached, so every result is in the SDRAM the testbench reads after the exit)
 *   TAB + 64 + j * 0x800: +0 N1  +4 N2  +8 nev (1..4)  +12..+24 ev[4]  +28 the refresh-phase delay (DT iterations
 *                         before the job; soc_tb's retry of a timing mismatch, M3-fix)
 *                         +32 state block (80 words)   +352 scratch image (512 bytes)   +864 code block (1024 bytes)
 *   results, j * 64: per event k, +16 k: cycles(N1), events(N1), cycles(N2), events(N2)
 * The run ends with exit code 0 through the sim-control port (0xB8000000).
 */
        .little
        .text
        .global _start
_start:
        /* the table header -> DHDR (the driver's data: a safe cache index) */
        mov.l   tab_p, r2
        mov.l   dhdr_p, r1
        mov     #16, r3
0:      mov.l   @r2+, r0
        mov.l   r0, @r1
        dt      r3
        bf/s    0b
        add     #4, r1
        /* counter 2: elapsed Icyc (KOS perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES)) */
        mov.l   pmcr2_p, r1
        mov     #0, r0
        mov.w   r0, @r1
        mov.w   w_clr, r0
        mov.w   r0, @r1
        mov.w   w_run23, r0
        mov.w   r0, @r1
        mov     #0, r10                 /* r10 = job index */

job_loop:
        mov.l   dhdr_p, r1
        mov.l   @(4,r1), r0             /* njobs */
        cmp/hs  r0, r10
        bt      all_done
        /* r11 = TAB + 64 + j * 0x800 */
        mov.l   tab_p, r11
        mov     r10, r0
        shll8   r0
        shll2   r0
        shll    r0
        add     r0, r11
        add     #64, r11
        /* the job header (32 bytes) -> JHDR */
        mov     r11, r2
        mov.l   jhdr_p, r1
        mov     #8, r3
1:      mov.l   @r2+, r0
        mov.l   r0, @r1
        dt      r3
        bf/s    1b
        add     #4, r1
        /* the job's refresh-phase delay (job + 28, M3-fix): that many DT iterations before the job, so that a point
           soc_tb retries runs its timed windows at another phase of the free-running refresh timer (SOC.md 5, 9.27);
           0 for every first run */
        mov.l   jhdr_p, r1
        mov.l   @(28,r1), r0
        tst     r0, r0
        bt      12f
13:     dt      r0
        bf      13b
12:
        /* measure(): the code block -> jit_code */
        mov     r11, r2
        mov.w   w_code_off, r0
        add     r0, r2
        mov.l   dhdr_p, r1
        mov.l   @(12,r1), r1            /* &jit_code */
        mov.w   w_256, r3
2:      mov.l   @r2+, r0
        mov.l   r0, @r1
        dt      r3
        bf/s    2b
        add     #4, r1
        /* dcache_flush_range(jit_code, 1024) */
        mov.l   dhdr_p, r1
        mov.l   @(12,r1), r1
        mov     #32, r3
3:      ocbwb   @r1
        dt      r3
        bf/s    3b
        add     #32, r1
        /* icache_flush_range(jit_code, 1024) */
        mov.l   dhdr_p, r4
        mov.l   @(12,r4), r4
        mov.w   w_1024, r5
        bsr     iflush
        nop
        /* state_build -> the runner's state */
        mov     r11, r2
        add     #32, r2
        mov.l   dhdr_p, r1
        mov.l   @(20,r1), r1            /* &state */
        mov     #80, r3
4:      mov.l   @r2+, r0
        mov.l   r0, @r1
        dt      r3
        bf/s    4b
        add     #4, r1
        /* scratch_build -> the image buffer */
        mov     r11, r2
        mov.w   w_scr_off, r0
        add     r0, r2
        mov.l   simg_p, r1
        mov.w   w_128, r3
5:      mov.l   @r2+, r0
        mov.l   r0, @r1
        dt      r3
        bf/s    5b
        add     #4, r1

        /* the events */
        mov.l   rbuf_p, r13             /* r13 = where run_once stores (cycles, events) */
        mov     #0, r12                 /* r12 = event index */
ev_loop:
        mov.l   jhdr_p, r1
        mov.l   @(8,r1), r0             /* nev */
        cmp/hs  r0, r12
        bt      ev_done
        mov     r12, r0
        shll2   r0
        add     r1, r0
        mov.l   @(12,r0), r2            /* ev[k] */
        /* counter 1 = the event (KOS perf_cntr_start(PRFC0, event, PMCR_COUNT_CPU_CYCLES): stop, clear, run) */
        mov.l   pmcr1_p, r1
        mov     #0, r0
        mov.w   r0, @r1
        mov.w   w_clr, r0
        mov.w   r0, @r1
        mov.w   w_run, r0
        or      r2, r0
        mov.w   r0, @r1
        tst     r12, r12
        bf      6f
        mov.l   jhdr_p, r1              /* the warm-up: run_once(N1), result dropped */
        mov.l   @(0,r1), r5
        bsr     run_once
        nop
        add     #-8, r13
6:      mov.l   jhdr_p, r1
        mov.l   @(0,r1), r5             /* N1 */
        bsr     run_once
        nop
        mov.l   jhdr_p, r1
        mov.l   @(4,r1), r5             /* N2 */
        bsr     run_once
        nop
        bra     ev_loop
        add     #1, r12
ev_done:
        /* the job's results (16 words) -> result base + j * 64 */
        mov.l   dhdr_p, r1
        mov.l   @(24,r1), r1
        mov     r10, r0
        shll2   r0
        shll2   r0
        shll2   r0
        add     r0, r1
        mov.l   rbuf_p, r2
        mov     #16, r3
7:      mov.l   @r2+, r0
        mov.l   r0, @r1
        dt      r3
        bf/s    7b
        add     #4, r1
        bra     job_loop
        add     #1, r10

all_done:
        mov.l   exit_p, r1
        mov     #0, r0
        mov.l   r0, @r1
8:      bra     8b
        nop

/* run_once(r5 = iterations): stores (cycles, events) of the call at @r13, @(4,r13) and advances r13 by 8.  r8 / r9 hold
   the counters' start values across jit_enter (jit_enter / jit_exit keep r8-r15, PR's saved copy included); r14 keeps
   this routine's return address */
run_once:
        sts     pr, r14
        mov.l   dhdr_p, r1
        mov.l   @(20,r1), r4            /* &state */
        mov.l   r5, @(52,r4)            /* state[13] = iterations */
        mov.l   @(16,r1), r1            /* &scratch */
        mov.l   simg_p, r2
        mov.w   w_128, r3
9:      mov.l   @r2+, r0                /* memcpy(scratch, image, 512) */
        mov.l   r0, @r1
        dt      r3
        bf/s    9b
        add     #4, r1
        mov.l   dhdr_p, r1
        mov.l   @(16,r1), r1
        mov     #16, r3
10:     ocbwb   @r1                     /* dcache_flush_range(scratch, 512) */
        dt      r3
        bf/s    10b
        add     #32, r1
        mov.l   dhdr_p, r1
        mov.l   @(8,r1), r3             /* &jit_enter */
        mov.l   @(20,r1), r4            /* &state */
        mov.l   bcr2_p, r1
        mov.w   @r1, r0                 /* Bclk-class read: the timed window starts at a fixed Bclk phase */
        mov.l   pmctr2_p, r1
        mov.l   pmctr1_p, r2
        mov.l   @r1, r8                 /* c0 = PMCTR2L (KOS PMCTR_LO(1)) */
        mov.l   @r2, r9                 /* e0 = PMCTR1L (KOS PMCTR_LO(0)) */
        jsr     @r3
        nop
        mov.l   pmctr2_p, r1
        mov.l   pmctr1_p, r2
        mov.l   @r1, r3                 /* c1 */
        mov.l   @r2, r4                 /* e1 */
        sub     r8, r3
        sub     r9, r4
        mov.l   r3, @r13
        mov.l   r4, @(4,r13)
        add     #8, r13
        lds     r14, pr
        rts
        nop

/* icache_flush_range(r4 = start, r5 = size) as KOS 2.1's arch/dreamcast/kernel/cache.s: from P2, per line an OCBWB and
   an IC address-array write of the line's tag with V = 0, then 7 instructions before returning to P1 */
iflush:
        mov.l   iflush_real_p, r0
        mov.l   p2_or, r1
        or      r1, r0
        jmp     @r0
        nop
        .align 2
iflush_real:
        add     r4, r5
        mov.l   align_mask, r0
        and     r0, r4
        mov.l   ica_addr, r1
        mov.l   ic_entry_mask, r2
        mov.l   ic_valid_mask, r3
11:     ocbwb   @r4
        mov     r4, r6
        and     r2, r6
        or      r1, r6
        mov     r4, r7
        and     r3, r7
        add     #32, r4
        cmp/hi  r4, r5
        bt/s    11b
        mov.l   r7, @r6
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        rts
        nop

        .align 2
w_clr:      .word   0x2000
w_run23:    .word   0xC023
w_run:      .word   0xC000
w_code_off: .word   864
w_scr_off:  .word   352
w_256:      .word   256
w_128:      .word   128
w_1024:     .word   1024
        .align 2
tab_p:      .long   0x8C200000
dhdr_p:     .long   dhdr
jhdr_p:     .long   jhdr
rbuf_p:     .long   rbuf
simg_p:     .long   simg
pmcr1_p:    .long   0xFF000084
pmcr2_p:    .long   0xFF000088
pmctr1_p:   .long   0xFF100008
pmctr2_p:   .long   0xFF100010
bcr2_p:     .long   0xFF800004
exit_p:     .long   0xB8000000
iflush_real_p: .long iflush_real
p2_or:      .long   0xA0000000
align_mask: .long   0xFFFFFFE0
ica_addr:   .long   0xF0000000
ic_entry_mask: .long 0x00001FE0
ic_valid_mask: .long 0xFFFFFC00

/* the driver's data: 1 KB after the code (DRV + 0x400: cache index 0x060) */
        .org    0x400
simg:   .space  512                     /* the scratch image (OC 0x060-0x06F) */
dhdr:   .space  64                      /* the table header (OC 0x070-0x071) */
jhdr:   .space  32                      /* the job header (OC 0x072) */
rbuf:   .space  64                      /* this job's results (OC 0x073-0x074) */
        .end
