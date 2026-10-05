/* dmac3.c -- what the CPU can do while a burst-mode DMA transfer runs (rtl/v1/spec/dmac.md, after dmac2).
 *
 * *** DO NOT RUN.  On 2026-09-23 04:10 this program HUNG THE CONSOLE (no output at all; hwrun timed out after 300 s and
 * *** the next upload stalled until about 04:27).  Every loop in it is bounded, so one of the CPU accesses it makes while a
 * *** 2048 x 32-byte auto-request burst holds the bus (PMCTR / TMU / CCN / DMAC register read, DMAC register write,
 * *** uncached RAM read or write, Holly register read, or the DE = 0 write of U2) never completed.  Which one is not
 * *** known.  A follow-up must test ONE access per run, starting with the least likely to hang.
 *
 * dmac2 T4 could not tell an instruction-cache miss from a real stall, so here every probe runs twice back to back
 * (the first pass warms the instruction and operand caches, only the second is kept), with no call and no printf in
 * between; the cycle counter is read directly (PMCTR1L).
 *   U1  probes: P4 reads (PMCTR, TMU, CCN, DMAC), P4 write, cached and uncached RAM, Holly register, ALU loop
 *   U2  can the CPU suspend a burst (write DE = 0 while it runs)?
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arch/irq.h>
#include <arch/cache.h>
#include <dc/perfctr.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define DMAC_BASE 0xFFA00000u
#define SAR(c)   (DMAC_BASE + (c) * 0x10u + 0x0u)
#define DAR(c)   (DMAC_BASE + (c) * 0x10u + 0x4u)
#define TCR(c)   (DMAC_BASE + (c) * 0x10u + 0x8u)
#define CHCR(c)  (DMAC_BASE + (c) * 0x10u + 0xCu)
#define PMCTR1L  0xFF100010u
#define PHYS(p) ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define P2(p)   (PHYS(p) | 0xA0000000u)
#define CFG_BURST32 (0x4000u | 0x1000u | 0x0400u | 0x80u | 0x40u)     /* DM inc, SM inc, auto, burst, 32 B */
#define UNITS 2048u

static uint8_t bufA[65536] __attribute__((aligned(32)));
static uint8_t bufB[65536] __attribute__((aligned(32)));
static volatile uint32_t cword[8] __attribute__((aligned(32)));
static uint32_t res[16][3];

/* ALU loop fully in registers: dt/bf, about 2 cycles per iteration */
#define ALU(n) do { uint32_t _n = (n); __asm__ volatile("1: dt %0\n\tbf 1b" : "+r"(_n) : : "t"); } while (0)

static void __attribute__((noinline)) probes(void) {
    for (int p = 0; p < 12; p++)
        for (int pass = 0; pass < 2; pass++) {
            uint32_t units = pass ? UNITS : 16u;
            volatile uint32_t sink = 0;
            sink += cword[0];
            (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
            R32(SAR(1)) = PHYS(bufA); R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = units;
            uint32_t ta = 0, t0 = R32(PMCTR1L);
            R32(CHCR(1)) = CFG_BURST32 | 1u;
            switch (p) {
                case 0: break;                                             /* PMCTR read right away */
                case 1: ALU(40000); break;                                 /* cache-resident ALU loop */
                case 2: sink += R32(0xFFD8000C); ALU(40000); break;        /* TMU TCNT0 */
                case 3: sink += R32(0xFF000000); ALU(40000); break;        /* CCN PTEH */
                case 4: sink += R32(TCR(1)); ALU(40000); break;            /* DMAC DMATCR1 */
                case 5: R32(SAR(3)) = 0; ALU(40000); break;                /* DMAC SAR3 write */
                case 6: sink += cword[1]; ALU(40000); break;               /* cached RAM, line resident */
                case 7: sink += R32(P2(bufA) + 0x10000u - 64u); ALU(40000); break;   /* uncached RAM read */
                case 8: R32(P2(bufA) + 0x10000u - 64u) = 1; ALU(40000); break;      /* uncached RAM write */
                case 9: sink += R32(0xA05F6900u); ALU(40000); break;       /* Holly SB_ISTNRM (area 0) */
                case 10: ta = R32(PMCTR1L); ALU(40000); break;             /* PMCTR read, then ALU */
                case 11: ALU(40000); ta = R32(PMCTR1L); break;             /* ALU, then PMCTR read */
            }
            uint32_t t1 = R32(PMCTR1L);
            uint32_t n = 0; while (R32(TCR(1)) != 0 && ++n < 50000000u) {}
            uint32_t t2 = R32(PMCTR1L);
            (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
            if (pass) { res[p][0] = t1 - t0; res[p][1] = t2 - t0; res[p][2] = ta ? ta - t0 : 0; }
            (void)sink;
        }
    /* reference: the ALU loop alone, and the burst alone */
    for (int pass = 0; pass < 2; pass++) {
        uint32_t t0 = R32(PMCTR1L); ALU(40000); uint32_t t1 = R32(PMCTR1L);
        res[12][0] = t1 - t0;
    }
}

/* U2: write DE = 0 some time after the burst starts */
static void __attribute__((noinline)) suspend(void) {
    for (int k = 0; k < 3; k++)
        for (int pass = 0; pass < 2; pass++) {
            (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
            R32(SAR(1)) = PHYS(bufA); R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = UNITS;
            uint32_t t0 = R32(PMCTR1L);
            R32(CHCR(1)) = CFG_BURST32 | 1u;
            if (k == 1) ALU(5000);
            if (k == 2) ALU(15000);
            R32(CHCR(1)) = CFG_BURST32;                                    /* DE = 0 */
            uint32_t t1 = R32(PMCTR1L);
            uint32_t rem = R32(TCR(1));
            uint32_t n = 0; while (R32(TCR(1)) != 0 && ++n < 200000u) {}
            uint32_t rem2 = R32(TCR(1));
            (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
            if (pass) { res[13 + k][0] = t1 - t0; res[13 + k][1] = rem; res[13 + k][2] = rem2; }
        }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    dcache_purge_range((uintptr_t)bufA, sizeof bufA);
    dcache_purge_range((uintptr_t)bufB, sizeof bufB);
    irq_mask_t old = irq_disable();
    probes();
    suspend();
    irq_restore(old);
    static const char *nm[12] = {"PMCTR read right away", "ALU(40000) from cache", "TMU TCNT0 read + ALU",
                                 "CCN PTEH read + ALU", "DMAC DMATCR1 read + ALU", "DMAC SAR3 write + ALU",
                                 "cached RAM hit + ALU", "uncached RAM read + ALU", "uncached RAM write + ALU",
                                 "Holly ISTNRM read + ALU", "PMCTR read (ta), then ALU", "ALU, then PMCTR read (ta)"};
    printf("dmac3: ALU(40000) alone %lu cyc; burst %u x 32B RAM->RAM\n", (unsigned long)res[12][0], UNITS);
    for (int p = 0; p < 12; p++)
        printf("U1 %-28s t1 %7lu  burst-end seen %7lu  ta %7lu\n", nm[p], (unsigned long)res[p][0],
               (unsigned long)res[p][1], (unsigned long)res[p][2]);
    for (int k = 0; k < 3; k++)
        printf("U2 DE=0 written after ALU(%5d): write+PMCTR at %7lu, DMATCR right after %lu, after polling %lu\n",
               k == 0 ? 0 : k == 1 ? 5000 : 15000, (unsigned long)res[13 + k][0], (unsigned long)res[13 + k][1],
               (unsigned long)res[13 + k][2]);
    perf_cntr_stop(PRFC1);
    return 0;
}
