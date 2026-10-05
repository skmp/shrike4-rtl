/* dmac2_bm.c -- the bare-metal part of the dmac2 console probe (tools/hw/dmac/dmac2.c, compiled unmodified: it is
 * #included here) for the v1 SoC testbench (WI-DMAC; README.md).  The console's main() becomes dmac2_console_main()
 * and is not called.  This main() prints the console's first and last lines and runs T2 (DMTE with DE = 0), T3
 * (address-error classes), T4 (the CPU during a burst) and T5 (the CPU against cycle steal).  T1 (CPU writes to the
 * DDT channel 0 while Holly drives maple) is left for the full KOS build at M6. */
#define main dmac2_console_main
#include "dmac2.c"
#undef main

int main(void) {
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    dcache_purge_range((uintptr_t)bufA, sizeof bufA);
    dcache_purge_range((uintptr_t)bufB, sizeof bufB);
    printf("dmac2: bufA %08lx bufB %08lx DMAOR %08lx\n", (unsigned long)PHYS(bufA), (unsigned long)PHYS(bufB),
           (unsigned long)R32(DMAOR));
    t2(); t3(); t4(); t5();
    printf("dmac2: end DMAOR %08lx CHCR0-3 %08lx %08lx %08lx %08lx\n", (unsigned long)R32(DMAOR),
           (unsigned long)R32(CHCR(0)), (unsigned long)R32(CHCR(1)), (unsigned long)R32(CHCR(2)),
           (unsigned long)R32(CHCR(3)));
    perf_cntr_stop(PRFC1);
    return 0;
}
