/* cachetest.c -- SH7091 instruction cache, operand cache, OC RAM, store queues: functional facts and timing.
 *
 * Part 1 (timing): kern.S's run_kernel prepares the operand-cache state of a region (purge / touch / dirty
 * victims ...), then times one body function with the performance counters (PMCTR1 = elapsed CPU cycles, PMCTR0 =
 * one event of EVS[]).  Every test is run REPS times per event; the minimum cycle count is reported, per unit
 * (x100), with the event counts of the fastest run.  Interrupts are masked while a kernel runs.
 * Part 2 (functional): memory-mapped cache arrays (read through a P2 helper), OCBI / OCBP / OCBWB / MOVCA.L / PREF
 * effects, address-array writes, write-through, OC RAM and index modes (CCR written from P2, OC purged around every
 * mode switch), store-queue QACR0 / QACR1 routing, SQ-area reads.
 * Output: one line per result; the program returns 0.  Run only through tools/hw/hwrun.sh. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <arch/cache.h>
#include <dc/perfctr.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define CCR_A 0xFF00001C
#define QACR0_A 0xFF000038
#define QACR1_A 0xFF00003C
#define P2(x) (((uint32_t)(x) & 0x1FFFFFFF) | 0xA0000000)
#define P0(x) ((uint32_t)(x) & 0x1FFFFFFF)
#define OCIDX(x) (((uint32_t)(x) >> 5) & 511)

typedef struct { uint32_t mode, pbase, pn, pstride, fn, a0, a1, a2, a3, cyc, ev; } kdesc_t;
extern void run_kernel(kdesc_t *k);
extern char b_empty[], b_chain[], b_ldstream[], b_ld2[], b_ldhot[], b_ldgap[], b_ststream[], b_stgap[], b_prefld[],
    b_prefstream[], b_ocbwb[], b_ocbp[], b_ocbi[], b_movca[], b_sqnp[], b_sq1[], b_sq2[], b_sqpp[], b_sqw[],
    b_sq1gap[], blkA[], blkB[], blkC[], blkC_entry[], icprobe[], try_rd_insn[];
extern uint32_t p2_rd(uint32_t a);
extern uint32_t p2_wr(uint32_t a, uint32_t v);
extern uint32_t set_ccr_p2(uint32_t v, uint32_t purge);
extern uint32_t ccr_time_p2(uint32_t v, uint32_t purge);
extern void ic_probe_p2(uint32_t fn, uint32_t a1, uint32_t a2, uint32_t *out);
extern uint32_t try_rd(uint32_t a, uint32_t marker);

enum { P_NONE = 0, P_OCBP = 1, P_TOUCH = 2, P_DIRTYV = 3, P_DIRTY = 4, P_CLEANV = 5, P_WARM = 6, P_OCBI = 7 };
static const uint8_t EVS[] = { 0x04, 0x05, 0x22, 0x25, 0x08, 0x21, 0x24 };
static const char *EVN[] = { "rdmiss", "wrmiss", "ocfill", "frzoc", "icmiss", "icfill", "frzic" };
#define NEV (sizeof EVS / sizeof EVS[0])
#define REPS 5
#define NL 64                               /* lines per data test */

static uint8_t big[0x70000] __attribute__((aligned(16384)));
static kdesc_t kd __attribute__((aligned(32)));
static uint32_t hot_word __attribute__((aligned(32)));
static int hot_idx[64], n_hot;

static uint32_t oca_rd(unsigned e) { return p2_rd(0xF4000000u | (e << 5)); }
static uint32_t ocd_rd(unsigned e, unsigned w) { return p2_rd(0xF5000000u | (e << 5) | (w << 2)); }
static uint32_t ica_rd(unsigned e) { return p2_rd(0xF0000000u | (e << 5)); }
static uint32_t icd_rd(unsigned e, unsigned w) { return p2_rd(0xF1000000u | (e << 5) | (w << 2)); }
static void ocbp(uint32_t a) { __asm__ volatile("ocbp @%0" :: "r"(a) : "memory"); }
static void ocbi(uint32_t a) { __asm__ volatile("ocbi @%0" :: "r"(a) : "memory"); }
static void ocbwb(uint32_t a) { __asm__ volatile("ocbwb @%0" :: "r"(a) : "memory"); }
static void pref(uint32_t a) { __asm__ volatile("pref @%0" :: "r"(a) : "memory"); }
static void movca(uint32_t a, uint32_t v) {
    register uint32_t r0 __asm__("r0") = v;
    __asm__ volatile("movca.l r0,@%0" :: "r"(a), "r"(r0) : "memory");
}
static void spin(int n) { for (volatile int i = 0; i < n; i++) ; }

/* region with NL lines at stride s whose OC indexes avoid the hot lines (stack, kd, hot_word) */
static int collides(uint32_t base, unsigned n, unsigned stride) {
    for (unsigned i = 0; i < n; i++) {
        int ix = OCIDX(base + i * stride);
        for (int h = 0; h < n_hot; h++) if (ix == hot_idx[h]) return 1;
    }
    return 0;
}
static uint32_t pick_region(uint32_t lo, uint32_t hi, unsigned n, unsigned stride) {
    for (uint32_t b = lo; b + (n - 1) * stride + 32 + 16384 <= hi; b += 32)
        if (!collides(b, n, stride)) return b;
    printf("pick_region failed\n");
    return lo;
}
static void set_hot(void) {
    volatile uint32_t here;
    uint32_t sp = (uint32_t)&here;
    n_hot = 0;
    for (int d = -256; d <= 256; d += 32) hot_idx[n_hot++] = OCIDX(sp + d);
    hot_idx[n_hot++] = OCIDX(&kd); hot_idx[n_hot++] = OCIDX((uint32_t)&kd + 32);
    hot_idx[n_hot++] = OCIDX(&hot_word);
}
static void chain_build(uint32_t base, unsigned n, unsigned stride, unsigned woff, uint32_t ptrmask_or) {
    for (unsigned i = 0; i < n; i++) {
        uint32_t node = base + i * stride + woff, next = base + ((i + 1) % n) * stride + woff;
        R32(node) = (next & 0x1FFFFFFF) | ptrmask_or;
    }
}

static void pr_result(const char *name, unsigned units, uint32_t best, const uint32_t *ev) {
    printf("T %-26s n=%3u cyc=%6lu c/u=%4lu.%02lu", name, units, (unsigned long)best, (unsigned long)(best / units),
           (unsigned long)((best % units) * 100 / units));
    for (unsigned e = 0; e < NEV; e++)
        if (ev[e]) printf(" %s=%lu.%02lu", EVN[e], (unsigned long)(ev[e] / units), (unsigned long)((ev[e] % units) * 100 / units));
    printf("\n");
}
typedef void (*prehook_t)(void);
static uint32_t measure_raw(kdesc_t *k, prehook_t pre, uint32_t *ev) {
    uint32_t best = ~0u;
    for (unsigned e = 0; e < NEV; e++) {
        perf_cntr_start(PRFC0, (perf_cntr_event_t)EVS[e], PMCR_COUNT_CPU_CYCLES);
        uint32_t bc = ~0u, be = 0;
        for (int r = 0; r < REPS; r++) {
            irq_mask_t im = irq_disable();
            if (pre) pre();
            run_kernel(k);
            irq_restore(im);
            if (k->cyc < bc) { bc = k->cyc; be = k->ev; }
        }
        ev[e] = be;
        if (bc < best) best = bc;
    }
    return best;
}
static void measure(const char *name, unsigned units, uint32_t mode, uint32_t pbase, uint32_t pstride, void *fn,
                    uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, prehook_t pre) {
    kd.mode = mode; kd.pbase = pbase; kd.pn = NL; kd.pstride = pstride; kd.fn = (uint32_t)fn;
    kd.a0 = a0; kd.a1 = a1; kd.a2 = a2; kd.a3 = a3;
    uint32_t ev[NEV];
    uint32_t best = measure_raw(&kd, pre, ev);
    pr_result(name, units, best, ev);
}

/* ------------------------------------------------------------------------------------------------------------ */
static uint32_t icflush_addr, icflush_len;
static void pre_icflush(void) { icache_flush_range(icflush_addr, icflush_len); }

static void timing_tests(void) {
    uint32_t B = (uint32_t)big;
    set_hot();
    uint32_t R0 = pick_region(B, B + 0x8000, NL, 32);
    uint32_t R1 = pick_region(B + 0x8000, B + sizeof big, NL, 4128);
    printf("regions: R0 %08lx (OC idx %lu) R1 %08lx sp-idx %d kd %08lx\n", (unsigned long)R0, (unsigned long)OCIDX(R0),
           (unsigned long)R1, hot_idx[8], (unsigned long)(uint32_t)&kd);
    R32(&hot_word) = 0;

    measure("empty", 1, P_NONE, R0, 32, b_empty, 0, 0, 0, 0, NULL);
    /* read-miss latency: pointer chase */
    chain_build(R0, NL, 32, 0, 0x80000000);
    measure("chain hit s32 w0", NL, P_TOUCH, R0, 32, b_chain, R0, NL, 0, 0, NULL);
    measure("chain miss s32 w0", NL, P_OCBP, R0, 32, b_chain, R0, NL, 0, 0, NULL);
    measure("chain miss s32 w0 cleanV", NL, P_CLEANV, R0, 32, b_chain, R0, NL, 0, 0, NULL);
    measure("chain miss s32 w0 dirtyV", NL, P_DIRTYV, R0, 32, b_chain, R0, NL, 0, 0, NULL);
    chain_build(R0, NL, 32, 28, 0x80000000);
    measure("chain hit s32 w7", NL, P_TOUCH, R0, 32, b_chain, R0 + 28, NL, 0, 0, NULL);
    measure("chain miss s32 w7", NL, P_OCBP, R0, 32, b_chain, R0 + 28, NL, 0, 0, NULL);
    chain_build(R0, NL, 32, 12, 0x80000000);
    measure("chain miss s32 w3", NL, P_OCBP, R0, 32, b_chain, R0 + 12, NL, 0, 0, NULL);
    chain_build(R1, NL, 4128, 0, 0x80000000);
    measure("chain hit s4128", NL, P_TOUCH, R1, 4128, b_chain, R1, NL, 0, 0, NULL);
    measure("chain miss s4128", NL, P_OCBP, R1, 4128, b_chain, R1, NL, 0, 0, NULL);
    /* uncached (P2) load latency: the chain holds P2 pointers; the prep writes it back */
    chain_build(R0, NL, 32, 0, 0xA0000000);
    measure("chain P2 s32", NL, P_OCBP, R0, 32, b_chain, P2(R0), NL, 0, 0, NULL);
    chain_build(R1, NL, 4128, 0, 0xA0000000);
    measure("chain P2 s4128", NL, P_OCBP, R1, 4128, b_chain, P2(R1), NL, 0, 0, NULL);
    /* read-miss throughput and what may run under a fill */
    measure("ldstream hit", NL, P_TOUCH, R0, 32, b_ldstream, R0, NL, 32, 0, NULL);
    measure("ldstream miss", NL, P_OCBP, R0, 32, b_ldstream, R0, NL, 32, 0, NULL);
    measure("ldstream miss dirtyV", NL, P_DIRTYV, R0, 32, b_ldstream, R0, NL, 32, 0, NULL);
    measure("ld2 hit", NL, P_TOUCH, R0, 32, b_ld2, R0, NL, 32, 0, NULL);
    measure("ld2 miss", NL, P_OCBP, R0, 32, b_ld2, R0, NL, 32, 0, NULL);
    measure("ldhot hit", NL, P_TOUCH, R0, 32, b_ldhot, R0, NL, 32, (uint32_t)&hot_word, NULL);
    measure("ldhot miss", NL, P_OCBP, R0, 32, b_ldhot, R0, NL, 32, (uint32_t)&hot_word, NULL);
    for (uint32_t g = 4; g <= 64; g *= 2) {
        char nm[40];
        snprintf(nm, sizeof nm, "ldgap%lu hit", (unsigned long)g);
        measure(nm, NL, P_TOUCH, R0, 32, b_ldgap, R0, NL, 32, g, NULL);
        snprintf(nm, sizeof nm, "ldgap%lu miss", (unsigned long)g);
        measure(nm, NL, P_OCBP, R0, 32, b_ldgap, R0, NL, 32, g, NULL);
        snprintf(nm, sizeof nm, "ldgap%lu miss dirtyV", (unsigned long)g);
        measure(nm, NL, P_DIRTYV, R0, 32, b_ldgap, R0, NL, 32, g, NULL);
    }
    measure("ldstream P2 s32", NL, P_OCBP, R0, 32, b_ldstream, P2(R0), NL, 32, 0, NULL);
    /* stores, copy-back */
    measure("ststream hit", NL, P_TOUCH, R0, 32, b_ststream, R0, NL, 32, 0, NULL);
    measure("ststream miss", NL, P_OCBP, R0, 32, b_ststream, R0, NL, 32, 0, NULL);
    measure("ststream miss dirtyV", NL, P_DIRTYV, R0, 32, b_ststream, R0, NL, 32, 0, NULL);
    for (uint32_t g = 4; g <= 64; g *= 2) {
        char nm[40];
        snprintf(nm, sizeof nm, "stgap%lu hit", (unsigned long)g);
        measure(nm, NL, P_TOUCH, R0, 32, b_stgap, R0, NL, 32, g, NULL);
        snprintf(nm, sizeof nm, "stgap%lu miss", (unsigned long)g);
        measure(nm, NL, P_OCBP, R0, 32, b_stgap, R0, NL, 32, g, NULL);
        snprintf(nm, sizeof nm, "stgap%lu P2 s4", (unsigned long)g);
        measure(nm, NL, P_OCBP, R0, 32, b_stgap, P2(R0), NL, 4, g, NULL);
    }
    /* uncached stores */
    measure("ststream P2 s4", NL, P_OCBP, R0, 32, b_ststream, P2(R0), NL, 4, 0, NULL);
    measure("ststream P2 s8", NL, P_OCBP, R0, 32, b_ststream, P2(R0), NL, 8, 0, NULL);
    measure("ststream P2 s32", NL, P_OCBP, R0, 32, b_ststream, P2(R0), NL, 32, 0, NULL);
    /* PREF */
    measure("prefstream hit", NL, P_TOUCH, R0, 32, b_prefstream, R0, NL, 32, 0, NULL);
    measure("prefstream miss", NL, P_OCBP, R0, 32, b_prefstream, R0, NL, 32, 0, NULL);
    measure("prefstream miss dirtyV", NL, P_DIRTYV, R0, 32, b_prefstream, R0, NL, 32, 0, NULL);
    measure("prefstream P2", NL, P_OCBP, R0, 32, b_prefstream, P2(R0), NL, 32, 0, NULL);
    for (uint32_t d = 1; d <= 64; d = d < 8 ? d + 1 : d + 8) {
        char nm[40];
        snprintf(nm, sizeof nm, "prefld d%lu hit", (unsigned long)d);
        measure(nm, NL, P_TOUCH, R0, 32, b_prefld, R0, NL, 32, d, NULL);
        snprintf(nm, sizeof nm, "prefld d%lu miss", (unsigned long)d);
        measure(nm, NL, P_OCBP, R0, 32, b_prefld, R0, NL, 32, d, NULL);
    }
    /* cache-block ops */
    measure("ocbwb invalid", NL, P_OCBP, R0, 32, b_ocbwb, R0, NL, 32, 0, NULL);
    measure("ocbwb clean", NL, P_TOUCH, R0, 32, b_ocbwb, R0, NL, 32, 0, NULL);
    measure("ocbwb dirty", NL, P_DIRTY, R0, 32, b_ocbwb, R0, NL, 32, 0, NULL);
    measure("ocbp invalid", NL, P_OCBP, R0, 32, b_ocbp, R0, NL, 32, 0, NULL);
    measure("ocbp clean", NL, P_TOUCH, R0, 32, b_ocbp, R0, NL, 32, 0, NULL);
    measure("ocbp dirty", NL, P_DIRTY, R0, 32, b_ocbp, R0, NL, 32, 0, NULL);
    measure("ocbi invalid", NL, P_OCBP, R0, 32, b_ocbi, R0, NL, 32, 0, NULL);
    measure("ocbi clean", NL, P_TOUCH, R0, 32, b_ocbi, R0, NL, 32, 0, NULL);
    measure("ocbi dirty", NL, P_DIRTY, R0, 32, b_ocbi, R0, NL, 32, 0, NULL);
    measure("movca hit", NL, P_TOUCH, R0, 32, b_movca, R0, NL, 32, 0, NULL);
    measure("movca miss", NL, P_OCBP, R0, 32, b_movca, R0, NL, 32, 0, NULL);
    measure("movca miss dirtyV", NL, P_DIRTYV, R0, 32, b_movca, R0, NL, 32, 0, NULL);
    measure("movca P2", NL, P_OCBP, R0, 32, b_movca, P2(R0), NL, 32, 0, NULL);
    measure("ocbwb dirty P0alias", NL, P_DIRTY, R0, 32, b_ocbwb, P0(R0), NL, 32, 0, NULL);
    ocbp(R0);

    /* store queues to area 3 (the region R1 area; QACR0 = QACR1 = area 3) */
    {
        uint32_t q0 = R32(QACR0_A), q1 = R32(QACR1_A);
        R32(QACR0_A) = 0x0C; R32(QACR1_A) = 0x0C;
        uint32_t sq = 0xE0000000u | (P0(R1) & 0x03FFFFC0);
        for (uint32_t i = 0; i < NL * 64; i += 32) ocbp(R1 + i);    /* the destination stays out of the OC */
        measure("sq stores only", NL, P_NONE, R0, 32, b_sqnp, sq, NL, 64, 0, NULL);
        measure("sq SQ0 only", NL, P_NONE, R0, 32, b_sq1, sq, NL, 64, 0, NULL);
        measure("sq SQ0/SQ1 alternate", NL / 2, P_NONE, R0, 32, b_sq2, sq, NL / 2, 64, 0, NULL);
        measure("sq pref0 pref1", NL, P_NONE, R0, 32, b_sqpp, sq, NL, 0, 0, NULL);
        measure("sq pref0 st0", NL, P_NONE, R0, 32, b_sqw, sq, NL, 0, 0, NULL);
        for (uint32_t g = 1; g <= 64; g *= 2) {
            char nm[40];
            snprintf(nm, sizeof nm, "sq SQ0 gap%lu", (unsigned long)g);
            measure(nm, NL, P_NONE, R0, 32, b_sq1gap, sq, NL, 64, g, NULL);
        }
        R32(QACR0_A) = q0; R32(QACR1_A) = q1;
    }

    /* write-through (CCR.WT = 1 applies to P0): the region through its P0 alias */
    {
        uint32_t ccr0 = R32(CCR_A);
        irq_mask_t im = irq_disable();
        set_ccr_p2(ccr0 | 0x2, 0);
        irq_restore(im);
        measure("WT ststream hit s4", NL, P_TOUCH, P0(R0), 32, b_ststream, P0(R0), NL, 4, 0, NULL);
        measure("WT ststream hit s32", NL, P_TOUCH, P0(R0), 32, b_ststream, P0(R0), NL, 32, 0, NULL);
        measure("WT ststream miss s32", NL, P_OCBP, P0(R0), 32, b_ststream, P0(R0), NL, 32, 0, NULL);
        for (uint32_t g = 4; g <= 64; g *= 2) {
            char nm[40];
            snprintf(nm, sizeof nm, "WT stgap%lu hit s4", (unsigned long)g);
            measure(nm, NL, P_TOUCH, P0(R0), 32, b_stgap, P0(R0), NL, 4, g, NULL);
        }
        chain_build(R0, NL, 32, 0, 0x00000000);
        measure("WT chain miss", NL, P_OCBP, P0(R0), 32, b_chain, P0(R0), NL, 0, 0, NULL);
        im = irq_disable();
        set_ccr_p2(ccr0, 0);
        irq_restore(im);
    }

    /* instruction cache: cold = icache_flush_range of the block before every run */
    {
        struct { const char *nm; char *blk; uint32_t entry; } bl[3] = {
            { "icA straight", blkA, (uint32_t)blkA }, { "icB bra/line", blkB, (uint32_t)blkB },
            { "icC bra@24", blkC, (uint32_t)blkC_entry } };
        for (int i = 0; i < 3; i++) {
            char nm[40];
            snprintf(nm, sizeof nm, "%s warm", bl[i].nm);
            measure(nm, 32, P_WARM, R0, 32, (void *)bl[i].entry, 0, 0, 0, 0, NULL);
            icflush_addr = (uint32_t)bl[i].blk; icflush_len = 1024;
            snprintf(nm, sizeof nm, "%s cold", bl[i].nm);
            measure(nm, 32, P_NONE, R0, 32, (void *)bl[i].entry, 0, 0, 0, 0, pre_icflush);
            snprintf(nm, sizeof nm, "%s P2", bl[i].nm);
            measure(nm, 32, P_NONE, R0, 32, (void *)P2(bl[i].entry), 0, 0, 0, 0, NULL);
        }
    }
}

/* ------------------------------------------------------------------------------------------------------------ */
static void chk(const char *what, uint32_t got, uint32_t want) {
    printf("F %-44s %08lx  want %08lx  %s\n", what, (unsigned long)got, (unsigned long)want, got == want ? "ok" : "DIFF");
}
static void val(const char *what, uint32_t got) { printf("F %-44s %08lx\n", what, (unsigned long)got); }

static void functional_tests(void) {
    uint32_t B = (uint32_t)big;
    set_hot();
    /* scratch lines X (and X + 16 KB, same entry) away from the stack */
    uint32_t X = pick_region(B + 0x10000, B + 0x20000, 4, 32);
    uint32_t Y = X + 16384, e = OCIDX(X);
    uint32_t tagX = P0(X) & 0x1FFFFC00, tagY = P0(Y) & 0x1FFFFC00;
    uint32_t v[16];
    printf("F X=%08lx Y=%08lx entry %lu\n", (unsigned long)X, (unsigned long)Y, (unsigned long)e);
    val("CCR", R32(CCR_A));
    val("QACR0", R32(QACR0_A));
    val("QACR1", R32(QACR1_A));
    { uint32_t q = R32(QACR0_A); R32(QACR0_A) = 0xFFFFFFFF; val("QACR0 after write ffffffff", R32(QACR0_A)); R32(QACR0_A) = q; }

    /* address-array format after a load, a store, OCBWB, OCBI, OCBP */
    ocbp(X); ocbp(Y);
    R32(P2(X)) = 0xAAAA0001;
    v[0] = R32(X); v[1] = oca_rd(e);
    chk("load: value", v[0], 0xAAAA0001);
    chk("load: OC addr array (tag|V)", v[1], tagX | 1);
    v[2] = oca_rd(e) ; (void)v[2];
    R32(X) = 0xBBBB0002; v[3] = oca_rd(e); v[4] = R32(P2(X));
    chk("store hit: addr array (tag|U|V)", v[3], tagX | 3);
    chk("store hit copy-back: memory unchanged", v[4], 0xAAAA0001);
    v[5] = ocd_rd(e, 0);
    chk("OC data array word 0", v[5], 0xBBBB0002);
    ocbwb(X); v[6] = oca_rd(e); v[7] = R32(P2(X));
    chk("OCBWB: addr array", v[6], tagX | 1);
    chk("OCBWB: memory", v[7], 0xBBBB0002);
    R32(X) = 0xCCCC0003; ocbi(X); v[8] = oca_rd(e); v[9] = R32(P2(X)); v[10] = R32(X);
    chk("OCBI dirty: addr array V", v[8] & 1, 0);
    chk("OCBI dirty: memory keeps old", v[9], 0xBBBB0002);
    chk("OCBI dirty: reload", v[10], 0xBBBB0002);
    R32(X) = 0xDDDD0004; ocbp(X); v[11] = oca_rd(e); v[12] = R32(P2(X));
    chk("OCBP dirty: addr array V", v[11] & 1, 0);
    chk("OCBP dirty: memory", v[12], 0xDDDD0004);
    val("OCBP: addr array raw (tag kept?)", v[11]);
    /* OCBI through the P2 alias of a cached dirty line */
    v[0] = R32(X); R32(X) = 0xEEEE0005; ocbi(P2(X)); v[1] = oca_rd(e); v[2] = R32(P2(X));
    val("OCBI @P2 alias of dirty line: addr array", v[1]);
    val("OCBI @P2 alias: memory", v[2]);
    ocbp(X);
    /* OCBP through the P0 alias */
    v[0] = R32(X); R32(X) = 0xEEEE0006; ocbp(P0(X)); v[1] = oca_rd(e); v[2] = R32(P2(X));
    val("OCBP @P0 alias: addr array", v[1]);
    chk("OCBP @P0 alias: memory", v[2], 0xEEEE0006);
    ocbp(X);

    /* address-array writes */
    v[0] = R32(X); R32(X) = 0x11110007; p2_wr(0xF4000000u | (e << 5), 0); v[1] = oca_rd(e); v[2] = R32(P2(X));
    chk("AA write 0 (non-assoc) on dirty: V", v[1] & 3, 0);
    chk("AA write 0 (non-assoc) on dirty: written back", v[2], 0x11110007);
    v[0] = R32(X); R32(X) = 0x22220008; p2_wr(0xF4000000u | (e << 5), tagX | 1); v[1] = oca_rd(e); v[2] = R32(P2(X));
    chk("AA write tag|V (U=0, non-assoc) on dirty: entry", v[1], tagX | 1);
    chk("AA write tag|V (U=0, non-assoc) on dirty: memory", v[2], 0x22220008);
    R32(X) = 0x33330009; p2_wr(0xF4000008u | (e << 5), (X & 0xFFFFFC00) | 0); v[1] = oca_rd(e); v[2] = R32(P2(X));
    chk("AA assoc write match V=0 on dirty: V", v[1] & 1, 0);
    chk("AA assoc write match V=0 on dirty: memory", v[2], 0x33330009);
    v[0] = R32(X); R32(X) = 0x4444000A; p2_wr(0xF4000008u | (e << 5), (Y & 0xFFFFFC00) | 0); v[1] = oca_rd(e);
    chk("AA assoc write mismatch: entry unchanged", v[1], tagX | 3);
    p2_wr(0xF4000008u | (e << 5), (X & 0xFFFFFC00) | 1); v[1] = oca_rd(e); v[2] = R32(P2(X));
    chk("AA assoc write match U=0 V=1 on dirty: entry", v[1], tagX | 1);
    chk("AA assoc write match U=0 V=1 on dirty: memory", v[2], 0x4444000A);
    p2_wr(0xF4000008u | (e << 5), (P2(X) & 0xFFFFFC00) | 0); v[1] = oca_rd(e);
    val("AA assoc write with P2 address in data: entry", v[1]);
    ocbp(X);
    /* non-assoc write of a tag with bits 31:29 set, then read back */
    p2_wr(0xF4000000u | (e << 5), 0xE0000000u | tagY | 0); v[1] = oca_rd(e);
    val("AA write e0000000|tagY V=0: read back", v[1]);
    p2_wr(0xF4000000u | (e << 5), 0); ocbp(X); ocbp(Y);
    /* data-array write does not set U */
    v[0] = R32(X); p2_wr(0xF5000000u | (e << 5) | 4, 0x5555000B); v[1] = oca_rd(e); v[2] = R32(X + 4); v[3] = R32(P2(X + 4));
    chk("DA write: addr array (U not set)", v[1], tagX | 1);
    chk("DA write: cached value", v[2], 0x5555000B);
    val("DA write: memory (old)", v[3]);
    ocbi(X);

    /* MOVCA.L: allocation without fill */
    for (int k = 0; k < 8; k++) { R32(P2(X) + 4 * k) = 0xC0DE0000 + k; R32(P2(Y) + 4 * k) = 0x5A5A0000 + k; }
    v[0] = R32(X);                                           /* X clean in the entry */
    movca(Y, 0x1111AAAA);
    for (int k = 0; k < 8; k++) v[1 + k] = R32(Y + 4 * k);
    v[9] = oca_rd(e);
    chk("MOVCA miss (clean victim X): entry", v[9], tagY | 3);
    for (int k = 0; k < 8; k++) { char nm[48]; snprintf(nm, sizeof nm, "MOVCA miss: Y word %d (X was C0DE000k)", k); val(nm, v[1 + k]); }
    ocbi(Y);
    v[0] = R32(X); R32(X + 8) = 0x7777000C;                   /* X dirty in the entry */
    movca(Y, 0x2222BBBB); v[1] = R32(P2(X + 8)); v[2] = oca_rd(e); v[3] = R32(Y + 8);
    chk("MOVCA miss (dirty victim): X written back", v[1], 0x7777000C);
    chk("MOVCA miss (dirty victim): entry", v[2], tagY | 3);
    val("MOVCA miss (dirty victim): Y word 2", v[3]);
    ocbi(Y);
    movca(P2(Y), 0x3333CCCC); v[1] = oca_rd(e); v[2] = R32(P2(Y));
    val("MOVCA @P2: addr array", v[1]);
    chk("MOVCA @P2: memory", v[2], 0x3333CCCC);

    /* PREF */
    ocbp(X); pref(X); spin(20); v[1] = oca_rd(e);
    chk("PREF miss: entry", v[1], tagX | 1);
    ocbp(X); pref(P2(X)); spin(20); v[1] = oca_rd(e);
    val("PREF @P2: entry", v[1]);
    ocbp(X); pref(P0(X)); spin(20); v[1] = oca_rd(e);
    chk("PREF @P0 alias: entry", v[1], tagX | 1);
    ocbp(X);
    /* a read hit on a line loaded through the other alias (tags are physical) */
    ocbp(X); v[0] = R32(P0(X)); v[1] = oca_rd(e);
    chk("load via P0 then entry", v[1], tagX | 1);
    ocbp(X);

    /* write-through */
    {
        uint32_t ccr0 = R32(CCR_A);
        irq_mask_t im = irq_disable();
        set_ccr_p2(ccr0 | 0x2, 0);
        ocbp(X);
        R32(P2(X)) = 0x10000001;
        R32(P0(X)) = 0x20000002; v[1] = oca_rd(e); v[2] = R32(P2(X));
        v[3] = R32(P0(X)); v[4] = oca_rd(e);
        R32(P0(X)) = 0x30000003; v[5] = oca_rd(e); v[6] = R32(P2(X));
        ocbp(X); ocbp(Y);
        movca(P0(Y), 0x4000AAAA); v[7] = oca_rd(e); v[8] = R32(P2(Y));
        ocbp(Y);
        set_ccr_p2(ccr0, 0);
        irq_restore(im);
        chk("WT store miss: no allocate (V)", v[1] & 1, 0);
        chk("WT store miss: memory", v[2], 0x20000002);
        chk("WT load miss: allocates", v[4], tagX | 1);
        chk("WT store hit: U stays 0", v[5], tagX | 1);
        chk("WT store hit: memory", v[6], 0x30000003);
        val("WT MOVCA miss: entry", v[7]);
        chk("WT MOVCA miss: memory", v[8], 0x4000AAAA);
    }

    /* IC arrays: run blkA, read its first line */
    {
        void (*f)(void) = (void (*)(void))blkA;
        f();
        unsigned ie = ((uint32_t)blkA >> 5) & 255;
        v[0] = ica_rd(ie); v[1] = icd_rd(ie, 0); v[2] = icd_rd(ie, 7);
        chk("IC addr array of blkA line 0", v[0], (P0(blkA) & 0x1FFFFC00) | 1);
        chk("IC data array word 0", v[1], R32(blkA));
        chk("IC data array word 7", v[2], R32((uint32_t)blkA + 28));
    }

    /* store queues: QACR0 -> area 3 (RAM), QACR1 -> area 1 (VRAM 32-bit path); a line >= 1 MB into RAM */
    {
        uint8_t *m = memalign(64, 0x180000);
        uint32_t off = 0;
        if (m) {
            uint32_t T = ((uint32_t)m + 0x100000 + 63) & ~63u;     /* P1 */
            off = P0(T) & 0x00FFFFC0;                               /* offset in the 16 MB RAM */
            if (off < 0x100000) off = 0;
            uint32_t q0 = R32(QACR0_A), q1 = R32(QACR1_A);
            uint32_t vram = 0xA5000000u | off | 0x20, ramsq1 = 0xAC000000u | off | 0x20, ram0 = 0xAC000000u | off;
            for (int k = 0; k < 8; k++) { R32(vram + 4 * k) = 0xFFFF0000 + k; R32(ramsq1 + 4 * k) = 0xEEEE0000 + k; R32(ram0 + 4 * k) = 0xDDDD0000 + k; }
            for (uint32_t i = 0; i < 64; i += 32) ocbp(T + i);
            irq_mask_t im = irq_disable();
            R32(QACR0_A) = 0x0C; R32(QACR1_A) = 0x04;
            uint32_t sq1 = 0xE0000000u | ((0x01000000u | off) & 0x03FFFFC0) | 0x20;
            uint32_t sq0 = 0xE0000000u | (off & 0x03FFFFC0);
            for (int k = 0; k < 8; k++) R32(sq1 + 4 * k) = 0x51510000 + k;
            pref(sq1);
            R32(sq1) = 0;                                          /* waits for the transfer */
            for (int k = 0; k < 8; k++) R32(sq0 + 4 * k) = 0x50500000 + k;
            pref(sq0 + 0x14);                                      /* PREF address bits 4:2 are don't-care */
            R32(sq0) = 0;
            for (int k = 0; k < 8; k++) { v[k] = R32(vram + 4 * k); v[8 + k] = R32(ramsq1 + 4 * k); }
            uint32_t r0v[8];
            for (int k = 0; k < 8; k++) r0v[k] = R32(ram0 + 4 * k);
            /* read the SQ area back (SQ0 now holds 0, 50500001..; SQ1 0, 51510001..) */
            uint32_t sqr0 = try_rd(0xE0000004u, 0xDEADBEEF), sqr1 = try_rd(0xE0000024u, 0xDEADBEEF);
            R32(QACR0_A) = q0; R32(QACR1_A) = q1;
            irq_restore(im);
            printf("F SQ test line off %08lx sq0 %08lx sq1 %08lx\n", (unsigned long)off, (unsigned long)sq0, (unsigned long)sq1);
            chk("SQ1 via QACR1=area1: VRAM word 1", v[1], 0x51510001);
            chk("SQ1 via QACR1=area1: VRAM word 7", v[7], 0x51510007);
            chk("SQ1 via QACR1: RAM (QACR0 view) untouched", v[9], 0xEEEE0001);
            chk("SQ0 via QACR0=area3: RAM word 1", r0v[1], 0x50500001);
            chk("SQ0 (PREF @+0x14): RAM word 7", r0v[7], 0x50500007);
            val("SQ area read E0000004", sqr0);
            val("SQ area read E0000024", sqr1);
            free(m);
        }
    }
}

/* exception catch for try_rd: skip the faulting load */
static volatile uint32_t g_exc_code, g_exc_pc;
static void on_exc(irq_t code, irq_context_t *ctx, void *data) {
    (void)data;
    g_exc_code = code; g_exc_pc = ctx->pc;
    if (ctx->pc == (uint32_t)try_rd_insn) ctx->pc += 2;
    else ctx->pc += 2;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* OC RAM (ORA), OIX, IIX, CCR invalidation timing.  Interrupts masked throughout; the OC is purged before every
 * CCR change (RAM-mode entries are skipped when leaving RAM mode; OCI then clears every V / U). */
static void mode_tests(void) {
    uint32_t B = (uint32_t)big;
    uint32_t ccr0 = R32(CCR_A);
    uint32_t v[40];
    static const uint32_t ra[] = { 0x7C000000, 0x7C001000, 0x7C002000, 0x7C003000, 0x7C004000, 0x7C005000,
                                   0x7DFFF000, 0x7E000000, 0x7E001000, 0x7E002000, 0x7FFFF000, 0x7C000FE0 };
    const int nra = sizeof ra / sizeof ra[0];
    set_hot();
    uint32_t X = pick_region(B + 0x11000, B + 0x20000, 1, 32);      /* address bit 12 = 1 wanted */
    while (!((X >> 12) & 1)) X += 0x1000;
    uint32_t chainres[2] = { 0, 0 }, chainev[NEV];

    irq_mask_t im = irq_disable();
    set_ccr_p2(ccr0 | 0x20, 1);                                      /* ORA = 1 (OIX = 0) */
    for (int i = 0; i < nra; i++) R32(ra[i]) = 0xA0000000u + i;
    for (int i = 0; i < nra; i++) v[i] = R32(ra[i]);
    uint32_t d128 = ocd_rd(128, 0), d384 = ocd_rd(384, 0), a128 = oca_rd(128), a384 = oca_rd(384), d255 = ocd_rd(255, 0);
    uint32_t d511 = ocd_rd(511, 0);
    /* cached index in RAM mode: X has address bit 12 = 1 */
    ocbp(X); (void)R32(X);
    uint32_t xi = OCIDX(X), xlo = xi & ~128u, xhi = xi | 128u;
    uint32_t ax_lo = oca_rd(xlo), ax_hi = oca_rd(xhi);
    ocbp(X);
    /* OC RAM load latency: pointer chase through 64 lines of RAM area 0x7C001000.. (one 8 KB window) */
    for (int i = 0; i < 64; i++) R32(0x7C001000u + 32 * i) = 0x7C001000u + 32 * ((i + 1) % 64);
    kd.mode = P_NONE; kd.pbase = 0; kd.pn = 0; kd.pstride = 32; kd.fn = (uint32_t)b_chain;
    kd.a0 = 0x7C001000; kd.a1 = 64; kd.a2 = 0; kd.a3 = 0;
    chainres[0] = measure_raw(&kd, NULL, chainev);
    perf_cntr_start(PRFC0, (perf_cntr_event_t)0x0B, PMCR_COUNT_CPU_CYCLES);
    run_kernel(&kd); chainres[1] = kd.ev;
    set_ccr_p2(ccr0 | 0x20 | 0x80, 2);                               /* ORA = 1, OIX = 1 */
    uint32_t w[16];
    for (int i = 0; i < nra; i++) R32(ra[i]) = 0xB0000000u + i;
    for (int i = 0; i < nra; i++) w[i] = R32(ra[i]);
    set_ccr_p2(ccr0 | 0x08, 2);                                      /* leave RAM mode, OCI */
    uint32_t ccr_after = R32(CCR_A);
    /* OIX alone: X (bit 13 = 1 wanted) through 0x8C.. and its 0x8E.. mirror */
    uint32_t Z = X; while (!((Z >> 13) & 1)) Z += 0x1000;
    set_ccr_p2(ccr0 | 0x80, 1);
    ocbp(Z); ocbp(Z | 0x02000000);
    (void)R32(Z); (void)R32(Z | 0x02000000);
    uint32_t zi = (Z >> 5) & 255;
    uint32_t oz0 = oca_rd(zi), oz1 = oca_rd(256 + zi);
    ocbp(Z); ocbp(Z | 0x02000000);
    set_ccr_p2(ccr0, 1);
    /* IIX: icprobe has address bit 12 = 1, bits 11:5 = 32 */
    uint32_t ip = (uint32_t)icprobe, ipr[6];
    set_ccr_p2(ccr0 | 0x8000 | 0x800, 0);
    ic_probe_p2(ip, 0xF0000000u | (32 << 5), 0xF0000000u | (160 << 5), &ipr[0]);
    ic_probe_p2(ip | 0x02000000, 0xF0000000u | (32 << 5), 0xF0000000u | (160 << 5), &ipr[2]);
    set_ccr_p2(ccr0 | 0x800, 0);
    ic_probe_p2(ip, 0xF0000000u | (32 << 5), 0xF0000000u | (160 << 5), &ipr[4]);
    /* invalidate-all timing across the CCR write */
    uint32_t t_plain = ccr_time_p2(ccr0, 1), t_oci = ccr_time_p2(ccr0 | 0x08, 1), t_ici = ccr_time_p2(ccr0 | 0x800, 1);
    uint32_t t_both = ccr_time_p2(ccr0 | 0x808, 1);
    irq_restore(im);

    for (int i = 0; i < nra; i++) printf("F ORA OIX=0 %08lx reads %08lx\n", (unsigned long)ra[i], (unsigned long)v[i]);
    val("ORA: OC data array entry 128 word 0", d128);
    val("ORA: OC data array entry 255 word 0", d255);
    val("ORA: OC data array entry 384 word 0", d384);
    val("ORA: OC data array entry 511 word 0", d511);
    val("ORA: OC addr array entry 128", a128);
    val("ORA: OC addr array entry 384", a384);
    printf("F ORA cached X=%08lx idx %lu: entry %lu = %08lx, entry %lu = %08lx (tag %08lx)\n", (unsigned long)X,
           (unsigned long)xi, (unsigned long)xlo, (unsigned long)ax_lo, (unsigned long)xhi, (unsigned long)ax_hi,
           (unsigned long)(P0(X) & 0x1FFFFC00));
    pr_result("ORA chain OC-RAM", 64, chainres[0], chainev);
    printf("F ORA chain event 0x0B (on-chip RAM operand accesses) per run: %lu\n", (unsigned long)chainres[1]);
    for (int i = 0; i < nra; i++) printf("F ORA OIX=1 %08lx reads %08lx\n", (unsigned long)ra[i], (unsigned long)w[i]);
    val("CCR after leaving RAM mode (OCI written)", ccr_after);
    printf("F OIX: Z=%08lx: entry %lu = %08lx, entry %lu = %08lx (tags %08lx / %08lx)\n", (unsigned long)Z,
           (unsigned long)zi, (unsigned long)oz0, (unsigned long)(256 + zi), (unsigned long)oz1,
           (unsigned long)(P0(Z) & 0x1FFFFC00), (unsigned long)((P0(Z) | 0x02000000) & 0x1FFFFC00));
    printf("F IIX=1 call %08lx: IC entry 32 = %08lx, entry 160 = %08lx\n", (unsigned long)ip, (unsigned long)ipr[0], (unsigned long)ipr[1]);
    printf("F IIX=1 call %08lx: IC entry 32 = %08lx, entry 160 = %08lx\n", (unsigned long)(ip | 0x02000000), (unsigned long)ipr[2], (unsigned long)ipr[3]);
    printf("F IIX=0 call %08lx: IC entry 32 = %08lx, entry 160 = %08lx\n", (unsigned long)ip, (unsigned long)ipr[4], (unsigned long)ipr[5]);
    printf("F CCR write cycles (P2 code): plain %lu OCI %lu ICI %lu both %lu\n", (unsigned long)t_plain,
           (unsigned long)t_oci, (unsigned long)t_ici, (unsigned long)t_both);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("cachetest: CCR %08lx QACR0 %08lx QACR1 %08lx MCR %08lx\n", (unsigned long)R32(CCR_A),
           (unsigned long)R32(QACR0_A), (unsigned long)R32(QACR1_A), (unsigned long)R32(0xFF800014));
    irq_cb_t old_r = irq_get_handler(EXC_DATA_ADDRESS_READ);
    irq_set_handler(EXC_DATA_ADDRESS_READ, on_exc, NULL);
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    timing_tests();
    functional_tests();
    mode_tests();
    perf_cntr_stop(PRFC0); perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    irq_set_handler(EXC_DATA_ADDRESS_READ, old_r.hdl, old_r.data);
    printf("exceptions caught: last code %03lx pc %08lx\n", (unsigned long)g_exc_code, (unsigned long)g_exc_pc);
    printf("cachetest DONE\n");
    return 0;
}
