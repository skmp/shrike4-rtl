/* dmac1_bm.c -- the bare-metal part of the dmac1 console probe (tools/hw/dmac/dmac1.c, compiled unmodified: it is
 * #included here) for the v1 SoC testbench (WI-DMAC; README.md; SOC.md 8.3 M3: "dmac1 / dmac2 bare-metal parts").
 * The console's main() becomes dmac1_console_main() and is not called.  This main() runs the sections that need
 * nothing from Holly, in the console's order and with the console's preamble: S2 (access widths), S3 (write masks,
 * DDT write masking, DMAOR), S4 (sizes, address forms), S5 (address modes), S6 (DMATCR = 0), S7 (suspend / resume),
 * S8 (TE), S9 (DMTE1 / DMAE, address errors), S10 (priorities), S11 (throughput), S12 (coherency).  Left out, for
 * the full KOS build at M6: S0 (the state KOS / the BIOS / Holly left), S1 (maple DMA), S13 (CH2-DMA through Holly). */
#define main dmac1_console_main
#include "dmac1.c"
#undef main

int main(void) {
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    dcache_purge_range((uintptr_t)bufA, sizeof bufA);
    dcache_purge_range((uintptr_t)bufB, sizeof bufB);
    printf("dmac1: bufA %08lx bufB %08lx\n", (unsigned long)PHYS(bufA), (unsigned long)PHYS(bufB));
    s2(); s3(); s4(); s5(); s6(); s7(); s8(); s9(); s10(); s11(); s12();
    perf_cntr_stop(PRFC1);
    printf("dmac1: done\n");
    return 0;
}
