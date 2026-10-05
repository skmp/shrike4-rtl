/* ddtprobe.c -- which DTRs Holly sends on SH4 DMAC channel 0 for G2-DMA, PVR-DMA and GD-DMA (rtl/v1/SOC.md open
 * question 9.11, owner WI-BOARD; board phase 2, rtl/v1/spec/board.md 4.3 / 10.6, dmac.md 4.2 / 9.4).
 *
 * Method: the channel-0 registers are what a DTR loads (dmac.md 3.9.3: CHCR0 from SZ / MD / R/W, DMATCR0 = COUNT,
 * SAR0 or DAR0 = ADDRESS), so sampling SAR0 / DAR0 / DMATCR0 / CHCR0 while a Holly DMA runs shows each DTR: the
 * register that moves gives R/W, its step gives the unit, DMATCR0 between a DTR and its transfer gives COUNT, CHCR0
 * gives SZ and MD.  Maple's DTRs were measured the same way (dmac1 S1: 32 B, MD 01, COUNT 1).
 *   A  G2-DMA RAM -> AICA RAM, 4 KB (KOS g2_dma_transfer, the AICA channel), sampled
 *   B  G2-DMA AICA RAM -> RAM, 4 KB, sampled
 *   C  PVR-DMA RAM -> texture memory, 4 KB (SB_PDSTAP / PDSTAR / PDLEN / PDST), sampled
 *   D  GD-DMA: one 2048-byte sector of the CD-R's data track (KOS cdrom_read_sectors_ex, DMA), ch0 before / after
 * Safety: built without KOS maple (INIT_IRQ | INIT_FS_PTY), so channel 0 is idle except for these DMAs; every loop
 * is bounded; IRQs are masked only around the sampled DMAs; the only accesses while Holly's DMA runs are channel-0
 * register reads, PMCTR and one Holly status read per sample -- the access pattern dmac1 S1 already ran during maple
 * DMA.  A PVR-DMA that does not finish in 100 ms is stopped with PDEN = 0 and reported.  The areas written (AICA RAM
 * 0x00820000, texture memory 0x04600000) are unused by KOS here.
 * Build: make ddtprobe.elf (KOS environment).  Run: ../hwrun.sh ddtprobe.elf.
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arch/cache.h>
#include <dc/g2bus.h>
#include <dc/cdrom.h>

KOS_INIT_FLAGS(INIT_IRQ | INIT_FS_PTY);

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define CYC    R32(0xFF100008)
#define SAR0    0xFFA00000u
#define DAR0    0xFFA00004u
#define DMATCR0 0xFFA00008u
#define CHCR0   0xFFA0000Cu
#define DMAOR   0xFFA00040u
#define ISTNRM  0xA05F6900u
#define ISTERR  0xA05F6908u

static uint8_t bufA[4096] __attribute__((aligned(32)));
static uint8_t bufB[4096] __attribute__((aligned(32)));

#define NS 700
static uint32_t smp[NS][5];

/* sample channel 0 until (R32(done) & mask) == 0 or tmo Icyc; records every change; returns the samples */
static int sample(uint32_t done, uint32_t mask, uint32_t tmo, uint32_t *tend) {
    int n = 0;
    uint32_t t0 = CYC, last = 0xFFFFFFFF, s, d, c, ch, t;
    for (;;) {
        s = R32(SAR0); d = R32(DAR0); c = R32(DMATCR0); ch = R32(CHCR0);
        const uint32_t sig = s ^ (d * 3) ^ (c * 7) ^ (ch * 13);
        t = CYC - t0;
        if (sig != last && n < NS) { smp[n][0] = t; smp[n][1] = s; smp[n][2] = d; smp[n][3] = c; smp[n][4] = ch; n++; last = sig; }
        if (!(R32(done) & mask)) break;
        if (t > tmo) { *tend = 0; return n; }
    }
    *tend = CYC - t0;
    /* one more sample after the end */
    if (n < NS) { smp[n][0] = CYC - t0; smp[n][1] = R32(SAR0); smp[n][2] = R32(DAR0); smp[n][3] = R32(DMATCR0);
                  smp[n][4] = R32(CHCR0); n++; }
    return n;
}

static void report(const char *tag, int n, uint32_t tend) {
    uint32_t maxtcr = 0, nch = 0, chv[8], sar_moves = 0, dar_moves = 0, tcr_nonzero = 0;
    uint32_t first_move = 0, last_move = 0;
    for (int i = 0; i < n; i++) {
        if (smp[i][3] > maxtcr) maxtcr = smp[i][3];
        if (smp[i][3]) tcr_nonzero++;
        int seen = 0;
        for (uint32_t k = 0; k < nch; k++) if (chv[k] == smp[i][4]) seen = 1;
        if (!seen && nch < 8) chv[nch++] = smp[i][4];
        if (i) {
            if (smp[i][1] != smp[i - 1][1]) { sar_moves++; if (!first_move) first_move = smp[i][0]; last_move = smp[i][0]; }
            if (smp[i][2] != smp[i - 1][2]) { dar_moves++; if (!first_move) first_move = smp[i][0]; last_move = smp[i][0]; }
        }
    }
    printf("B ddt %s end %lu samples %d SAR0-moves %lu DAR0-moves %lu DMATCR0-max %lu (nonzero in %lu samples) CHCR0",
           tag, (unsigned long)tend, n, (unsigned long)sar_moves, (unsigned long)dar_moves, (unsigned long)maxtcr,
           (unsigned long)tcr_nonzero);
    for (uint32_t k = 0; k < nch; k++) printf(" %08lx", (unsigned long)chv[k]);
    printf(" first-move %lu last-move %lu\n", (unsigned long)first_move, (unsigned long)last_move);
    for (int i = 0; i < n; i++) {
        if (i >= 24 && i < n - 8) continue;
        printf("B ddt %s t=%8lu SAR0 %08lx DAR0 %08lx DMATCR0 %08lx CHCR0 %08lx\n", tag, (unsigned long)smp[i][0],
               (unsigned long)smp[i][1], (unsigned long)smp[i][2], (unsigned long)smp[i][3], (unsigned long)smp[i][4]);
    }
}

static void snap(const char *tag) {
    printf("B ddt %s SAR0 %08lx DAR0 %08lx DMATCR0 %08lx CHCR0 %08lx DMAOR %08lx ISTNRM %08lx ISTERR %08lx\n", tag,
           (unsigned long)R32(SAR0), (unsigned long)R32(DAR0), (unsigned long)R32(DMATCR0), (unsigned long)R32(CHCR0),
           (unsigned long)R32(DMAOR), (unsigned long)R32(ISTNRM), (unsigned long)R32(ISTERR));
}

static void g2_test(const char *tag, int dir) {
    uint32_t tend = 0; int n;
    for (int i = 0; i < 4096; i++) bufA[i] = (uint8_t)(i * 7 + 3);
    memset(bufB, 0, sizeof bufB);
    dcache_flush_range((uintptr_t)bufA, sizeof bufA);
    dcache_flush_range((uintptr_t)bufB, sizeof bufB);
    g2_fifo_wait();
    snap(dir ? "g2in-before" : "g2out-before");
    int old = irq_disable();
    R32(ISTNRM) = 1u << 15;
    int rv = g2_dma_transfer(dir ? bufB : bufA, (void *)0x00820000, 4096, 0, NULL, NULL, dir, 0, 0, 0);
    if (rv == 0) n = sample(0xA05F7818, 1, 40000000u, &tend);    /* ADST: 1 while the AICA channel runs */
    else n = 0;
    uint32_t ist = R32(ISTNRM), err = R32(ISTERR);
    irq_restore(old);
    printf("B ddt %s rv %d ISTNRM %08lx ISTERR %08lx\n", tag, rv, (unsigned long)ist, (unsigned long)err);
    report(tag, n, tend);
    thd_sleep(20);                                              /* the G2 DMA IRQ handler runs */
    if (dir) {
        dcache_inval_range((uintptr_t)bufB, sizeof bufB);
        int bad = 0; for (int i = 0; i < 4096; i++) bad += bufB[i] != (uint8_t)(i * 7 + 3);
        printf("B ddt %s data mismatches %d\n", tag, bad);
    }
}

static void pvr_test(void) {
    uint32_t tend = 0; int n;
    for (int i = 0; i < 4096; i++) bufA[i] = (uint8_t)(i * 13 + 5);
    dcache_flush_range((uintptr_t)bufA, sizeof bufA);
    snap("pvr-before");
    int old = irq_disable();
    R32(ISTNRM) = 1u << 11; R32(ISTERR) = 0xC0;
    R32(0xA05F7C80) = 0x6702007F;                               /* PDAPRO: the whole of RAM */
    R32(0xA05F7C00) = 0x04600000;                               /* PDSTAP: texture memory, 64-bit path */
    R32(0xA05F7C04) = ((uint32_t)bufA) & 0x1FFFFFE0;            /* PDSTAR */
    R32(0xA05F7C08) = 4096;                                     /* PDLEN */
    R32(0xA05F7C0C) = 0;                                        /* PDDIR: RAM -> PVR */
    R32(0xA05F7C10) = 0;                                        /* PDTSEL: CPU trigger */
    R32(0xA05F7C14) = 1;                                        /* PDEN */
    R32(0xA05F7C18) = 1;                                        /* PDST */
    n = sample(0xA05F7C18, 1, 20000000u, &tend);
    if (!tend) { R32(0xA05F7C14) = 0; printf("B ddt pvr TIMEOUT: PDEN <- 0\n"); }
    uint32_t ist = R32(ISTNRM), err = R32(ISTERR);
    R32(ISTNRM) = 1u << 11; R32(ISTERR) = 0xC0;
    irq_restore(old);
    printf("B ddt pvr ISTNRM %08lx ISTERR %08lx PDSTAPD %08lx PDSTARD %08lx PDLEND %08lx\n", (unsigned long)ist,
           (unsigned long)err, (unsigned long)R32(0xA05F7CF0), (unsigned long)R32(0xA05F7CF4),
           (unsigned long)R32(0xA05F7CF8));
    report("pvr", n, tend);
    int bad = 0; for (int i = 0; i < 4096; i += 4) bad += R32(0xA4600000 + i) != *(uint32_t *)(bufA + i);
    printf("B ddt pvr data mismatches %d\n", bad);
}

static void gd_test(void) {
    CDROM_TOC toc;
    int st = -1, dt = -1;
    cdrom_init();                                               /* guarded: a no-op when KOS already did it */
    int rv = cdrom_get_status(&st, &dt);
    printf("B ddt gd status rv %d status %d disc %d\n", rv, st, dt);
    if (rv != ERR_OK) return;
    rv = cdrom_read_toc(&toc, false);
    if (rv != ERR_OK) { printf("B ddt gd toc %d\n", rv); return; }
    uint32_t lba = cdrom_locate_data_track(&toc);
    printf("B ddt gd data track lba %lu\n", (unsigned long)lba);
    if (!lba) return;
    memset(bufB, 0, sizeof bufB);
    dcache_flush_range((uintptr_t)bufB, sizeof bufB);
    snap("gd-before");
    uint32_t t0 = CYC;
    rv = cdrom_read_sectors_ex(bufB, lba, 1, CDROM_READ_DMA);
    uint32_t t1 = CYC;
    snap("gd-after");
    printf("B ddt gd read rv %d %lu Icyc buf %08lx..%08lx GDSTARD %08lx GDLEND %08lx first %02x%02x%02x%02x%02x%02x%02x%02x\n",
           rv, (unsigned long)(t1 - t0), (unsigned long)(((uint32_t)bufB) & 0x1FFFFFFF),
           (unsigned long)((((uint32_t)bufB) & 0x1FFFFFFF) + 2048), (unsigned long)R32(0xA05F74F4),
           (unsigned long)R32(0xA05F74F8), bufB[0], bufB[1], bufB[2], bufB[3], bufB[4], bufB[5], bufB[6], bufB[7]);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    snap("entry");
    printf("B ddt bufA %08lx bufB %08lx\n", (unsigned long)(((uint32_t)bufA) & 0x1FFFFFFF),
           (unsigned long)(((uint32_t)bufB) & 0x1FFFFFFF));
    g2_test("g2out", 0);
    g2_test("g2in", 1);
    pvr_test();
    gd_test();
    snap("exit");
    printf("B done\n");
    return 0;
}
