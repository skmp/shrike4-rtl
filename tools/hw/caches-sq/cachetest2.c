/* cachetest2.c -- second SH7091 cache / store-queue run (follow-ups to cachetest.c).
 *
 * Fixes the first run's functional part: there, a printf between steps let dcload's network code evict the test line
 * (the entry then showed dcload's BBA buffer, tag 01848000).  Here every functional result is recorded first and
 * printed only at the end.  Adds: MOVCA.L data words (clean / dirty / invalid victim, word offsets), stores and loads
 * to a line while its fill is in progress, PREF / MOVCA.L on P2 with a known other line in the entry, associative
 * writes (OC and IC), SQ-area and FF001000 reads, BSC refresh registers; timing of same-line accesses during a fill,
 * two misses per iteration, and the per-miss fraction versus the number of lines.
 * Run only through tools/hw/hwrun.sh. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <arch/cache.h>
#include <dc/perfctr.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define CCR_A 0xFF00001C
#define P2(x) (((uint32_t)(x) & 0x1FFFFFFF) | 0xA0000000)
#define P0(x) ((uint32_t)(x) & 0x1FFFFFFF)
#define OCIDX(x) (((uint32_t)(x) >> 5) & 511)

typedef struct { uint32_t mode, pbase, pn, pstride, fn, a0, a1, a2, a3, cyc, ev; } kdesc_t;
extern void run_kernel(kdesc_t *k);
extern char b_chain[], b_ldgap[], b_stld[], b_stst[], b_ldst[], b_stldo[], b_ldldo[], b_ldsto[], b_ld2gap[],
    b_stldgap[], b_ldstream[], b_ststream[], blkA[], try_rd_insn[];
extern uint32_t p2_rd(uint32_t a);
extern uint32_t p2_wr(uint32_t a, uint32_t v);
extern uint32_t try_rd(uint32_t a, uint32_t marker);

enum { P_NONE = 0, P_OCBP = 1, P_TOUCH = 2, P_DIRTYV = 3, P_DIRTY = 4, P_CLEANV = 5, P_WARM = 6, P_OCBI = 7 };
static const uint8_t EVS[] = { 0x04, 0x05, 0x22, 0x25 };
static const char *EVN[] = { "rdmiss", "wrmiss", "ocfill", "frzoc" };
#define NEV (sizeof EVS / sizeof EVS[0])
#define REPS 5

static uint8_t big[0x40000] __attribute__((aligned(16384)));
static kdesc_t kd __attribute__((aligned(32)));
static int hot_idx[64], n_hot;

static uint32_t oca_rd(unsigned e) { return p2_rd(0xF4000000u | (e << 5)); }
static uint32_t ocd_rd(unsigned e, unsigned w) { return p2_rd(0xF5000000u | (e << 5) | (w << 2)); }
static uint32_t ica_rd(unsigned e) { return p2_rd(0xF0000000u | (e << 5)); }
static void ocbp(uint32_t a) { __asm__ volatile("ocbp @%0" :: "r"(a) : "memory"); }
static void ocbi(uint32_t a) { __asm__ volatile("ocbi @%0" :: "r"(a) : "memory"); }
static void ocbwb(uint32_t a) { __asm__ volatile("ocbwb @%0" :: "r"(a) : "memory"); }
static void pref(uint32_t a) { __asm__ volatile("pref @%0" :: "r"(a) : "memory"); }
static void movca(uint32_t a, uint32_t v) {
    register uint32_t r0 __asm__("r0") = v;
    __asm__ volatile("movca.l r0,@%0" :: "r"(a), "r"(r0) : "memory");
}
static void spin(int n) { for (volatile int i = 0; i < n; i++) ; }

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
    for (int d = -512; d <= 256; d += 32) hot_idx[n_hot++] = OCIDX(sp + d);
    hot_idx[n_hot++] = OCIDX(&kd); hot_idx[n_hot++] = OCIDX((uint32_t)&kd + 32);
}
static void chain_build(uint32_t base, unsigned n, unsigned stride, unsigned woff) {
    for (unsigned i = 0; i < n; i++)
        R32(base + i * stride + woff) = base + ((i + 1) % n) * stride + woff;
}
static void pr_result(const char *name, unsigned units, uint32_t best, const uint32_t *ev) {
    printf("T %-26s n=%3u cyc=%6lu c/u=%4lu.%02lu", name, units, (unsigned long)best, (unsigned long)(best / units),
           (unsigned long)((best % units) * 100 / units));
    for (unsigned e = 0; e < NEV; e++)
        if (ev[e]) printf(" %s=%lu.%02lu", EVN[e], (unsigned long)(ev[e] / units), (unsigned long)((ev[e] % units) * 100 / units));
    printf("\n");
}
static void measure(const char *name, unsigned units, uint32_t mode, uint32_t pbase, uint32_t pn, uint32_t pstride,
                    void *fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    kd.mode = mode; kd.pbase = pbase; kd.pn = pn; kd.pstride = pstride; kd.fn = (uint32_t)fn;
    kd.a0 = a0; kd.a1 = a1; kd.a2 = a2; kd.a3 = a3;
    uint32_t ev[NEV], best = ~0u;
    for (unsigned e = 0; e < NEV; e++) {
        perf_cntr_start(PRFC0, (perf_cntr_event_t)EVS[e], PMCR_COUNT_CPU_CYCLES);
        uint32_t bc = ~0u, be = 0;
        for (int r = 0; r < REPS; r++) {
            irq_mask_t im = irq_disable();
            run_kernel(&kd);
            irq_restore(im);
            if (kd.cyc < bc) { bc = kd.cyc; be = kd.ev; }
        }
        ev[e] = be;
        if (bc < best) best = bc;
    }
    pr_result(name, units, best, ev);
}

static void timing_tests(void) {
    uint32_t B = (uint32_t)big;
    set_hot();
    uint32_t R0 = pick_region(B, B + 0x10000, 128, 32);
    printf("R0 %08lx idx %lu\n", (unsigned long)R0, (unsigned long)OCIDX(R0));
    char nm[48];
    static const unsigned ns[] = { 8, 16, 32, 64, 128 };
    for (unsigned i = 0; i < 5; i++) {
        unsigned n = ns[i];
        chain_build(R0, n, 32, 0);
        snprintf(nm, sizeof nm, "chain hit n%u", n);
        measure(nm, n, P_TOUCH, R0, n, 32, b_chain, R0, n, 0, 0);
        snprintf(nm, sizeof nm, "chain miss n%u", n);
        measure(nm, n, P_OCBP, R0, n, 32, b_chain, R0, n, 0, 0);
        snprintf(nm, sizeof nm, "ldgap4 miss n%u", n);
        measure(nm, n, P_OCBP, R0, n, 32, b_ldgap, R0, n, 32, 4);
        snprintf(nm, sizeof nm, "ldgap4 hit n%u", n);
        measure(nm, n, P_TOUCH, R0, n, 32, b_ldgap, R0, n, 32, 4);
    }
    /* same-line accesses while the fill is in progress */
    measure("stld hit", 64, P_TOUCH, R0, 64, 32, b_stld, R0, 64, 32, 0);
    measure("stld miss", 64, P_OCBP, R0, 64, 32, b_stld, R0, 64, 32, 0);
    measure("stst hit", 64, P_TOUCH, R0, 64, 32, b_stst, R0, 64, 32, 0);
    measure("stst miss", 64, P_OCBP, R0, 64, 32, b_stst, R0, 64, 32, 0);
    measure("ldst hit", 64, P_TOUCH, R0, 64, 32, b_ldst, R0, 64, 32, 0);
    measure("ldst miss", 64, P_OCBP, R0, 64, 32, b_ldst, R0, 64, 32, 0);
    for (uint32_t g = 1; g <= 8; g++) {
        snprintf(nm, sizeof nm, "ld2gap%lu hit", (unsigned long)g);
        measure(nm, 64, P_TOUCH, R0, 64, 32, b_ld2gap, R0, 64, 32, g);
        snprintf(nm, sizeof nm, "ld2gap%lu miss", (unsigned long)g);
        measure(nm, 64, P_OCBP, R0, 64, 32, b_ld2gap, R0, 64, 32, g);
        snprintf(nm, sizeof nm, "stldgap%lu hit", (unsigned long)g);
        measure(nm, 64, P_TOUCH, R0, 64, 32, b_stldgap, R0, 64, 32, g);
        snprintf(nm, sizeof nm, "stldgap%lu miss", (unsigned long)g);
        measure(nm, 64, P_OCBP, R0, 64, 32, b_stldgap, R0, 64, 32, g);
    }
    /* two misses per iteration: A line and B line (B = A + 2 KB) */
    measure("stldo hit", 64, P_TOUCH, R0, 128, 32, b_stldo, R0, 64, 32, 2048);
    measure("stldo miss", 64, P_OCBP, R0, 128, 32, b_stldo, R0, 64, 32, 2048);
    measure("ldldo hit", 64, P_TOUCH, R0, 128, 32, b_ldldo, R0, 64, 32, 2048);
    measure("ldldo miss", 64, P_OCBP, R0, 128, 32, b_ldldo, R0, 64, 32, 2048);
    measure("ldsto hit", 64, P_TOUCH, R0, 128, 32, b_ldsto, R0, 64, 32, 2048);
    measure("ldsto miss", 64, P_OCBP, R0, 128, 32, b_ldsto, R0, 64, 32, 2048);
    /* stride 64 (every other line) to see whether sequential lines matter */
    measure("ldstream miss s64", 64, P_OCBP, R0, 128, 32, b_ldstream, R0, 64, 64, 0);
    measure("ststream miss s64", 64, P_OCBP, R0, 128, 32, b_ststream, R0, 64, 64, 0);
}

/* ------------------------------------------------------------------------------------------------------------ */
typedef struct { const char *nm; uint32_t got, want; int w; } res_t;
static res_t res[256];
static int nres;
static void R(const char *nm, uint32_t got, uint32_t want) { if (nres < 256) { res[nres].nm = nm; res[nres].got = got; res[nres].want = want; res[nres].w = 1; nres++; } }
static void V(const char *nm, uint32_t got) { if (nres < 256) { res[nres].nm = nm; res[nres].got = got; res[nres].w = 0; nres++; } }
static volatile uint32_t g_exc_code, g_exc_pc, g_exc_n;
static void on_exc(irq_t code, irq_context_t *ctx, void *data) {
    (void)data;
    g_exc_code = code; g_exc_pc = ctx->pc; g_exc_n++;
    ctx->pc += 2;
}
static const char *wn[8] = { "w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7" };
static char namebuf[128][48];
static int nnb;
static const char *mk(const char *a, int k) { char *b = namebuf[nnb++ & 127]; snprintf(b, 48, "%s %s", a, wn[k]); return b; }

static void functional_tests(void) {
    uint32_t B = (uint32_t)big;
    set_hot();
    uint32_t X = pick_region(B + 0x10000, B + 0x20000, 1, 32), Y = X + 16384, e = OCIDX(X);
    uint32_t tagX = P0(X) & 0x1FFFFC00, tagY = P0(Y) & 0x1FFFFC00;
    uint32_t w[8];
    irq_mask_t im = irq_disable();

    /* dirty line: data array and OCBWB */
    ocbp(X); ocbp(Y);
    for (int k = 0; k < 8; k++) R32(P2(X) + 4 * k) = 0x10000000 + k;
    (void)R32(X); R32(X) = 0xBBBB0002;
    R("dirty: OC addr array", oca_rd(e), tagX | 3);
    R("dirty: OC data array w0", ocd_rd(e, 0), 0xBBBB0002);
    R("dirty: OC data array w1", ocd_rd(e, 1), 0x10000001);
    ocbwb(X);
    R("OCBWB dirty: addr array", oca_rd(e), tagX | 1);
    R("OCBWB dirty: memory", R32(P2(X)), 0xBBBB0002);
    /* associative writes */
    R32(X) = 0x4444000A;
    R("assoc U=0 V=1: before", oca_rd(e), tagX | 3);
    p2_wr(0xF4000008u | (e << 5), (X & 0xFFFFFC00) | 1);
    R("assoc U=0 V=1 on dirty: entry", oca_rd(e), tagX | 1);
    R("assoc U=0 V=1 on dirty: memory", R32(P2(X)), 0x4444000A);
    p2_wr(0xF4000008u | (e << 5), (P2(X) & 0xFFFFFC00) | 0);
    V("assoc data=P2 alias V=0: entry", oca_rd(e));
    (void)R32(X);
    p2_wr(0xF4000008u | (e << 5), (P0(X) & 0xFFFFFC00) | 0);
    V("assoc data=P0 alias V=0: entry", oca_rd(e));
    (void)R32(X);
    p2_wr(0xF4000008u | (e << 5), ((X | 0x02000000) & 0xFFFFFC00) | 0);
    V("assoc data=mirror (bit25) V=0: entry", oca_rd(e));
    ocbp(X);
    /* PREF / MOVCA.L on P2 with Y (clean) in the entry */
    (void)R32(Y);
    pref(P2(X)); spin(20);
    R("PREF @P2 X, Y in entry: entry", oca_rd(e), tagY | 1);
    movca(P2(X), 0x3333CCCC);
    R("MOVCA @P2 X, Y in entry: entry", oca_rd(e), tagY | 1);
    R("MOVCA @P2: memory", R32(P2(X)), 0x3333CCCC);
    ocbp(Y);
    /* MOVCA.L data words */
    for (int k = 0; k < 8; k++) { R32(P2(X) + 4 * k) = 0xC0DE0000 + k; R32(P2(Y) + 4 * k) = 0x5A5A0000 + k; }
    ocbp(X); ocbp(Y);
    (void)R32(X); movca(Y, 0x1111AAAA);
    for (int k = 0; k < 8; k++) w[k] = R32(Y + 4 * k);
    V("MOVCA w0, clean victim X: entry", oca_rd(e));
    for (int k = 0; k < 8; k++) V(mk("MOVCA w0 clean victim:", k), w[k]);
    ocbi(Y);
    (void)R32(X); movca(Y + 16, 0x2222BBBB);
    for (int k = 0; k < 8; k++) w[k] = R32(Y + 4 * k);
    for (int k = 0; k < 8; k++) V(mk("MOVCA w4 clean victim:", k), w[k]);
    ocbi(Y);
    (void)R32(X); movca(Y + 4, 0x5555EEEE);
    for (int k = 0; k < 8; k++) w[k] = R32(Y + 4 * k);
    for (int k = 0; k < 8; k++) V(mk("MOVCA w1 clean victim:", k), w[k]);
    ocbi(Y);
    (void)R32(X); R32(X + 8) = 0x7777000C; movca(Y, 0x3333CCCC);
    for (int k = 0; k < 8; k++) w[k] = R32(Y + 4 * k);
    R("MOVCA dirty victim: X written back", R32(P2(X) + 8), 0x7777000C);
    for (int k = 0; k < 8; k++) V(mk("MOVCA w0 dirty victim:", k), w[k]);
    ocbi(Y);
    (void)R32(X); ocbi(X); movca(Y, 0x4444DDDD);
    for (int k = 0; k < 8; k++) w[k] = R32(Y + 4 * k);
    for (int k = 0; k < 8; k++) V(mk("MOVCA w0 invalid entry (stale X):", k), w[k]);
    ocbi(Y);
    /* a store and a load to a line whose fill is in progress */
    for (int k = 0; k < 8; k++) R32(P2(X) + 4 * k) = 0x60000000 + k;
    ocbp(X);
    { uint32_t a = 0xA1, b = 0xA2; __asm__ volatile("mov.l %1,@%0\n\tmov.l %2,@(4,%0)" :: "r"(X), "r"(a), "r"(b) : "memory"); }
    spin(20);
    for (int k = 0; k < 8; k++) w[k] = R32(X + 4 * k);
    for (int k = 0; k < 8; k++) V(mk("store miss w0 + store w1 during fill:", k), w[k]);
    ocbp(X);
    for (int k = 0; k < 8; k++) R32(P2(X) + 4 * k) = 0x70000000 + k;
    { uint32_t t, b = 0xB2; __asm__ volatile("mov.l @%1,%0\n\tmov.l %2,@(4,%1)" : "=&r"(t) : "r"(X), "r"(b) : "memory"); }
    spin(20);
    for (int k = 0; k < 8; k++) w[k] = R32(X + 4 * k);
    for (int k = 0; k < 8; k++) V(mk("load miss w0 + store w1 during fill:", k), w[k]);
    ocbp(X);
    for (int k = 0; k < 8; k++) R32(P2(X) + 4 * k) = 0x80000000 + k;
    { uint32_t t, a = 0xC1; __asm__ volatile("mov.l %2,@%1\n\tmov.l @(4,%1),%0" : "=&r"(t) : "r"(X), "r"(a) : "memory"); w[0] = t; }
    R("store miss w0 + load w1 during fill: value", w[0], 0x80000001);
    ocbp(X);
    irq_restore(im);

    /* IC associative write */
    {
        void (*f)(void) = (void (*)(void))blkA;
        unsigned ie = ((uint32_t)blkA >> 5) & 255;
        im = irq_disable();
        f();
        uint32_t a0 = ica_rd(ie);
        p2_wr(0xF0000008u | (ie << 5), (((uint32_t)blkA + 0x4000) & 0xFFFFFC00) | 0);
        uint32_t a1 = ica_rd(ie);
        p2_wr(0xF0000008u | (ie << 5), ((uint32_t)blkA & 0xFFFFFC00) | 0);
        uint32_t a2 = ica_rd(ie);
        irq_restore(im);
        R("IC entry after running blkA", a0, (P0(blkA) & 0x1FFFFC00) | 1);
        R("IC assoc write mismatch: entry", a1, (P0(blkA) & 0x1FFFFC00) | 1);
        R("IC assoc write match V=0: entry", a2, (P0(blkA) & 0x1FFFFC00) | 0);
    }
    /* SQ area and FF001000 reads (SQ0 = 5000000k, SQ1 = 5100000k written, no PREF) */
    {
        uint32_t sqr[16], ffr[16], n0;
        im = irq_disable();
        for (int k = 0; k < 8; k++) { R32(0xE0000000u + 4 * k) = 0x50000000 + k; R32(0xE0000020u + 4 * k) = 0x51000000 + k; }
        n0 = g_exc_n;
        for (int k = 0; k < 16; k++) sqr[k] = try_rd(0xE0000000u + 4 * k, 0xDEADBEEF);
        uint32_t n1 = g_exc_n;
        for (int k = 0; k < 16; k++) ffr[k] = try_rd(0xFF001000u + 4 * k, 0xDEADBEEF);
        uint32_t n2 = g_exc_n;
        irq_restore(im);
        for (int k = 0; k < 16; k++) { char *b = namebuf[nnb++ & 127]; snprintf(b, 48, "read E00000%02x", 4 * k); V(b, sqr[k]); }
        V("exceptions during SQ-area reads", n1 - n0);
        for (int k = 0; k < 16; k++) { char *b = namebuf[nnb++ & 127]; snprintf(b, 48, "read FF0010%02x", 4 * k); V(b, ffr[k]); }
        V("exceptions during FF001000 reads", n2 - n1);
        V("last exception code", g_exc_code);
    }
    V("BSC RTCSR", R16(0xFF80001C));
    V("BSC RTCOR", R16(0xFF800024));
    V("BSC RFCR", R16(0xFF800028));
    V("BSC BCR1", R32(0xFF800000));
    V("BSC BCR2", R16(0xFF800004));
    V("BSC MCR", R32(0xFF800014));
    V("CPG FRQCR", R16(0xFFC00000));

    for (int i = 0; i < nres; i++) {
        if (res[i].w) printf("F %-46s %08lx  want %08lx  %s\n", res[i].nm, (unsigned long)res[i].got, (unsigned long)res[i].want,
                             res[i].got == res[i].want ? "ok" : "DIFF");
        else printf("F %-46s %08lx\n", res[i].nm, (unsigned long)res[i].got);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("cachetest2: CCR %08lx\n", (unsigned long)R32(CCR_A));
    irq_cb_t old_r = irq_get_handler(EXC_DATA_ADDRESS_READ);
    irq_set_handler(EXC_DATA_ADDRESS_READ, on_exc, NULL);
    functional_tests();
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    timing_tests();
    perf_cntr_stop(PRFC0); perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    irq_set_handler(EXC_DATA_ADDRESS_READ, old_r.hdl, old_r.data);
    printf("cachetest2 DONE\n");
    return 0;
}
