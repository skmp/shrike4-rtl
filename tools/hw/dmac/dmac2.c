/* dmac2.c -- follow-up DMAC hardware facts for rtl/v1/spec/dmac.md (after dmac1).
 *   T1  DDT mode: what a CPU write to SAR0 / DAR0 / DMATCR0 does (dmac1 S3 read 0 back after such a write)
 *   T2  DMTE level: TE = 1, IE = 1, DE = 0 -> interrupt?
 *   T3  address errors: on-chip module source/destination per RS, nonexistent module, single address mode
 *   T4  what the CPU can do while a burst-mode transfer holds the bus (ALU from cache, P4 reads, RAM reads)
 *   T5  CPU uncached reads interleaved with a cycle-steal transfer
 * Same safety rules as dmac1: bounded waits, maple idle and interrupts masked around DMAOR / channel 0 / AE, every
 * DE = 1 write with a nonzero DMATCR, registers restored.
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arch/irq.h>
#include <arch/cache.h>
#include <dc/perfctr.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R8(a)  (*(volatile uint8_t *)(uintptr_t)(a))
#define DMAC_BASE 0xFFA00000u
#define SAR(c)   (DMAC_BASE + (c) * 0x10u + 0x0u)
#define DAR(c)   (DMAC_BASE + (c) * 0x10u + 0x4u)
#define TCR(c)   (DMAC_BASE + (c) * 0x10u + 0x8u)
#define CHCR(c)  (DMAC_BASE + (c) * 0x10u + 0xCu)
#define DMAOR    (DMAC_BASE + 0x40u)
#define SB_MDST  0xA05F6C18u
#define SB_ISTNRM 0xA05F6900u
#define CH_DM(x) ((uint32_t)(x) << 14)
#define CH_SM(x) ((uint32_t)(x) << 12)
#define CH_RS(x) ((uint32_t)(x) << 8)
#define CH_TM    (1u << 7)
#define CH_TS(x) ((uint32_t)(x) << 4)
#define CH_IE    (1u << 2)
#define CH_TE    (1u << 1)
#define CH_DE    1u
#define PHYS(p) ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define P2(p)   (PHYS(p) | 0xA0000000u)

static uint8_t bufA[65536] __attribute__((aligned(32)));
static uint8_t bufB[65536] __attribute__((aligned(32)));
static volatile uint32_t cached_word[8] __attribute__((aligned(32)));

static inline uint32_t cyc(void) { return (uint32_t)perf_cntr_count(PRFC1); }
static void spin(uint32_t n) { uint32_t t0 = cyc(); while (cyc() - t0 < n) {} }
static int wait_tcr0(int ch, uint32_t maxc) {
    uint32_t t0 = cyc();
    while (R32(TCR(ch)) != 0) { if (cyc() - t0 > maxc) return -1; }
    return (int)(cyc() - t0);
}
static void ch_stop(int ch) { (void)R32(CHCR(ch)); R32(CHCR(ch)) = 0; }
static int maple_idle_wait(void) {
    for (int i = 0; i < 2000000; i++) if ((R32(SB_MDST) & 1) == 0) return 1;
    return 0;
}
static void clear_ae(void) { uint32_t o = R32(DMAOR); if (o & 4) R32(DMAOR) = o & ~4u; }

/* ---------------------------------------------------------------------------------------------------------------- */
static void t1(void) {
    uint32_t r[20]; int ok = 0;
    irq_mask_t old = irq_disable();
    if (maple_idle_wait()) {
        ok = 1;
        r[0] = R32(SAR(0)); r[1] = R32(DAR(0)); r[2] = R32(TCR(0)); r[3] = R32(CHCR(0));
        R32(TCR(0)) = 0;                                   /* same value it holds (0) */
        r[4] = R32(SAR(0)); r[5] = R32(DAR(0)); r[6] = R32(TCR(0));
        R32(SAR(0)) = 0x0C0DEAD0u;
        r[7] = R32(SAR(0)); r[8] = R32(DAR(0));
        R32(DAR(0)) = 0x0C0BEEF0u;
        r[9] = R32(SAR(0)); r[10] = R32(DAR(0));
        R32(TCR(0)) = 7;
        r[11] = R32(TCR(0));
        spin(2000);
        r[12] = R32(SAR(0)); r[13] = R32(DAR(0)); r[14] = R32(TCR(0)); r[15] = R32(CHCR(0));
    }
    irq_restore(old);
    if (!ok) { printf("T1 maple busy, skipped\n"); return; }
    printf("T1 before: SAR0 %08lx DAR0 %08lx DMATCR0 %08lx CHCR0 %08lx\n", (unsigned long)r[0], (unsigned long)r[1],
           (unsigned long)r[2], (unsigned long)r[3]);
    printf("T1 w DMATCR0=0: SAR0 %08lx DAR0 %08lx DMATCR0 %08lx | w SAR0=0C0DEAD0: SAR0 %08lx DAR0 %08lx | w "
           "DAR0=0C0BEEF0: SAR0 %08lx DAR0 %08lx | w DMATCR0=7: %08lx | 2000 cyc later SAR0 %08lx DAR0 %08lx DMATCR0 "
           "%08lx CHCR0 %08lx\n", (unsigned long)r[4], (unsigned long)r[5], (unsigned long)r[6], (unsigned long)r[7],
           (unsigned long)r[8], (unsigned long)r[9], (unsigned long)r[10], (unsigned long)r[11], (unsigned long)r[12],
           (unsigned long)r[13], (unsigned long)r[14], (unsigned long)r[15]);
    /* let KOS run maple for ~100 ms and look again */
    uint32_t t0 = cyc(); int mdma = 0, last = 0;
    while (cyc() - t0 < 20000000u) { int m = (int)(R32(SB_MDST) & 1); if (m && !last) mdma++; last = m; }
    printf("T1 after 100 ms (%d maple DMAs seen): SAR0 %08lx DAR0 %08lx DMATCR0 %08lx CHCR0 %08lx\n", mdma,
           (unsigned long)R32(SAR(0)), (unsigned long)R32(DAR(0)), (unsigned long)R32(TCR(0)),
           (unsigned long)R32(CHCR(0)));
}

/* ---------------------------------------------------------------------------------------------------------------- */
static volatile int h_cnt; static volatile uint32_t h_code, h_chcr;
static void h_dma(irq_t code, irq_context_t *ctx, void *d) {
    (void)ctx; (void)d;
    uint32_t c = R32(CHCR(1));
    if (h_cnt == 0) { h_code = (uint32_t)code; h_chcr = c; }
    h_cnt++;
    R32(CHCR(1)) = 0;
    clear_ae();
    if (h_cnt > 6) R16(0xFFD0000C) = R16(0xFFD0000C) & 0xF0FF;
}
static void t2(void) {
    uint32_t cfg = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(3);
    uint16_t iprc = R16(0xFFD0000C);
    irq_set_handler(0x660, h_dma, NULL);
    irq_set_handler(0x6C0, h_dma, NULL);
    h_cnt = 0;
    irq_mask_t old = irq_disable();
    ch_stop(1);
    R32(SAR(1)) = PHYS(bufA); R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = 4;
    R32(CHCR(1)) = cfg | CH_DE;
    (void)wait_tcr0(1, 1000000u); spin(200);
    uint32_t a = R32(CHCR(1));
    R32(CHCR(1)) = cfg | CH_TE | CH_IE;                 /* DE = 0, TE kept, IE set */
    uint32_t b = R32(CHCR(1));
    irq_restore(old);
    uint32_t t0 = cyc(); while (h_cnt == 0 && cyc() - t0 < 2000000u) {}
    int f = h_cnt;
    old = irq_disable(); ch_stop(1); irq_restore(old);
    printf("T2 TE=1 IE=1 DE=0: CHCR %08lx -> %08lx fired=%d code %03lx CHCR in handler %08lx\n", (unsigned long)a,
           (unsigned long)b, f, (unsigned long)h_code, (unsigned long)h_chcr);
    irq_set_handler(0x660, NULL, NULL);
    irq_set_handler(0x6C0, NULL, NULL);
    R16(0xFFD0000C) = iprc;
}

/* ---------------------------------------------------------------------------------------------------------------- */
struct aecase { const char *tag; uint32_t sar, dar, chcr, n; };
static void t3(void) {
    static const struct aecase cs[] = {
        {"RS=6 SAR=TSTR(1FD80004) byte SM=0 -> RAM", 0x1FD80004u, 0, CH_DM(1) | CH_SM(0) | CH_RS(6) | CH_TS(1), 4},
        {"RS=4 SAR=TSTR(1FD80004) byte ext->ext", 0x1FD80004u, 0, CH_DM(1) | CH_SM(0) | CH_RS(4) | CH_TS(1), 4},
        {"RS=4 SAR=TOCR(1FD80000) long ext->ext", 0x1FD80000u, 0, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3), 4},
        {"RS=6 SAR=RAM (not on-chip) long", 0xFFFFFFFFu, 0, CH_DM(1) | CH_SM(1) | CH_RS(6) | CH_TS(3), 4},
        {"RS=6 SAR=1F300000 (no module) long", 0x1F300000u, 0, CH_DM(1) | CH_SM(0) | CH_RS(6) | CH_TS(3), 4},
        {"RS=6 SAR=FFD80004 (P4 form) byte", 0xFFD80004u, 0, CH_DM(1) | CH_SM(0) | CH_RS(6) | CH_TS(1), 4},
        {"RS=2 (ext req single) SAR=1FD80004 byte", 0x1FD80004u, 0, CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(1), 4},
        {"RS=2 (ext req single) SAR=RAM+1 long", 0xFFFFFFFEu, 0, CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(3), 4},
        {"RS=4 DAR misaligned (+4) 32B", 0xFFFFFFFFu, 4, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(4), 2},
    };
    for (unsigned k = 0; k < sizeof cs / sizeof cs[0]; k++) {
        memset((void *)P2(bufB), 0xEE, 256);
        volatile uint32_t *a = (volatile uint32_t *)P2(bufA);
        for (int i = 0; i < 64; i++) a[i] = 0xA0000000u + (uint32_t)i;
        uint32_t sar = cs[k].sar == 0xFFFFFFFFu ? PHYS(bufA) : cs[k].sar == 0xFFFFFFFEu ? PHYS(bufA) + 1 : cs[k].sar;
        uint32_t dar = PHYS(bufB) + cs[k].dar;
        uint32_t r[6]; int ok = 0;
        irq_mask_t old = irq_disable();
        if (maple_idle_wait()) {
            ok = 1;
            ch_stop(1);
            R32(SAR(1)) = sar; R32(DAR(1)) = dar; R32(TCR(1)) = cs[k].n;
            R32(CHCR(1)) = cs[k].chcr | CH_DE;
            spin(3000);
            r[0] = R32(DMAOR); r[1] = R32(TCR(1)); r[2] = R32(CHCR(1)); r[3] = R32(SAR(1)); r[4] = R32(DAR(1));
            ch_stop(1);
            clear_ae();
            r[5] = R32(DMAOR);
        }
        irq_restore(old);
        if (!ok) { printf("T3 %s: maple busy\n", cs[k].tag); continue; }
        uint8_t *b = (uint8_t *)P2(bufB);
        printf("T3 %-46s DMAOR %08lx DMATCR %lu CHCR %08lx SAR %08lx DAR-start %+ld | dst %02x%02x%02x%02x %02x%02x%02x%02x "
               "%02x%02x%02x%02x | DMAOR after %08lx\n", cs[k].tag, (unsigned long)r[0], (unsigned long)r[1],
               (unsigned long)r[2], (unsigned long)r[3], (long)(r[4] - dar), b[3], b[2], b[1], b[0], b[7], b[6], b[5],
               b[4], b[11], b[10], b[9], b[8], (unsigned long)r[5]);
    }
    printf("T3 TSTR now %02x TOCR %02x\n", R8(0xFFD80004), R8(0xFFD80000));
}

/* ---------------------------------------------------------------------------------------------------------------- */
static uint32_t __attribute__((noinline)) alu_loop(uint32_t n) {
    uint32_t x = 1;
    for (uint32_t i = 0; i < n; i++) { x = x * 3 + i; __asm__ volatile("" : "+r"(x)); }
    return x;
}
#define BURST_UNITS 2048u
static uint32_t start_burst(void) {
    ch_stop(1);
    R32(SAR(1)) = PHYS(bufA); R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = BURST_UNITS;
    uint32_t t0 = cyc();
    R32(CHCR(1)) = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(4) | CH_DE;
    return t0;
}
static void t4(void) {
    volatile uint32_t sink = 0;
    /* reference timings without DMA */
    irq_mask_t old = irq_disable();
    uint32_t a0 = cyc(); sink += alu_loop(50000); uint32_t a1 = cyc();
    uint32_t tb0 = cyc(); ch_stop(1); R32(SAR(1)) = PHYS(bufA); R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = BURST_UNITS;
    R32(CHCR(1)) = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(4) | CH_DE; (void)wait_tcr0(1, 20000000u);
    uint32_t tb1 = cyc(); ch_stop(1);
    irq_restore(old);
    printf("T4 reference: alu_loop(50000) %lu cyc; burst of %u x 32B %lu cyc\n", (unsigned long)(a1 - a0), BURST_UNITS,
           (unsigned long)(tb1 - tb0));
    static const char *nm[] = {"none (cyc() right after)", "alu_loop(50000) then cyc()", "read TMU TCNT0 then alu",
                               "read CCN PTEH then alu", "read DMAC DMATCR1 then alu", "read P2 RAM then alu",
                               "read cached RAM (hit) then alu", "write DMAC SAR3 then alu", "read Holly ISTNRM then alu",
                               "write P2 RAM then alu"};
    for (int p = 0; p < 10; p++) {
        uint32_t t0, t1, t2; int w;
        cached_word[0] = 5; sink += cached_word[0];
        old = irq_disable();
        t0 = start_burst();
        switch (p) {
            case 0: break;
            case 1: sink += alu_loop(50000); break;
            case 2: sink += R32(0xFFD8000C); sink += alu_loop(50000); break;
            case 3: sink += R32(0xFF000000); sink += alu_loop(50000); break;
            case 4: sink += R32(TCR(1)); sink += alu_loop(50000); break;
            case 5: sink += R32(P2(bufA) + 0x10000u - 64); sink += alu_loop(50000); break;
            case 6: sink += cached_word[0]; sink += alu_loop(50000); break;
            case 7: R32(SAR(3)) = 0; sink += alu_loop(50000); break;
            case 8: sink += R32(SB_ISTNRM); sink += alu_loop(50000); break;
            case 9: R32(P2(bufA) + 0x10000u - 64) = 1; sink += alu_loop(50000); break;
        }
        t1 = cyc();
        w = wait_tcr0(1, 20000000u);
        t2 = cyc();
        ch_stop(1);
        irq_restore(old);
        printf("T4 probe %-32s: t1 %7lu  end %7lu (wait %d)\n", nm[p], (unsigned long)(t1 - t0),
               (unsigned long)(t2 - t0), w);
    }
    (void)sink;
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void t5(void) {
    volatile uint32_t *p2 = (volatile uint32_t *)P2(bufA) + 8192;   /* second half of A: not a DMA source here */
    volatile uint32_t sink = 0;
    for (int mode = 0; mode < 3; mode++) {
        irq_mask_t old = irq_disable();
        uint32_t d0 = 0, d1 = 0;
        if (mode) {
            ch_stop(1);
            R32(SAR(1)) = PHYS(bufA); R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = mode == 1 ? 8192 : 2048;  /* <= 64 KB */
            R32(CHCR(1)) = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(mode == 1 ? 3 : 4) | CH_DE;   /* cycle steal */
            spin(1000);
            d0 = R32(TCR(1));
        }
        uint32_t t0 = cyc();
        for (int i = 0; i < 2000; i++) sink += p2[(i * 8) & 4095];
        uint32_t t1 = cyc();
        if (mode) { d1 = R32(TCR(1)); ch_stop(1); }
        irq_restore(old);
        printf("T5 %s: 2000 uncached RAM reads %lu cyc (%.1f each); DMA units in that time %lu\n",
               mode == 0 ? "no DMA            " : mode == 1 ? "cycle steal 4B DMA" : "cycle steal 32B   ",
               (unsigned long)(t1 - t0), (t1 - t0) / 2000.0, (unsigned long)(d0 - d1));
    }
    (void)sink;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    dcache_purge_range((uintptr_t)bufA, sizeof bufA);
    dcache_purge_range((uintptr_t)bufB, sizeof bufB);
    printf("dmac2: bufA %08lx bufB %08lx DMAOR %08lx\n", (unsigned long)PHYS(bufA), (unsigned long)PHYS(bufB),
           (unsigned long)R32(DMAOR));
    t1(); fflush(stdout);
    t2(); fflush(stdout);
    t3(); fflush(stdout);
    t4(); fflush(stdout);
    t5(); fflush(stdout);
    printf("dmac2: end DMAOR %08lx CHCR0-3 %08lx %08lx %08lx %08lx\n", (unsigned long)R32(DMAOR),
           (unsigned long)R32(CHCR(0)), (unsigned long)R32(CHCR(1)), (unsigned long)R32(CHCR(2)),
           (unsigned long)R32(CHCR(3)));
    perf_cntr_stop(PRFC1);
    return 0;
}
