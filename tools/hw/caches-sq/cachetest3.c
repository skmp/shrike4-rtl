/* cachetest3.c -- third SH7091 cache run: confirms MOVCA.L's line contents with a dirty victim (run 2 read zeros),
 * MOVCA.L hit, and times: a second load to each quadword of a line in fill (critical-quadword forwarding and wrap
 * order), OCBP / OCBWB of a dirty line with a gap (write-back buffer), an OC load miss behind an SQ burst, and a P2
 * load behind a P2 store (write buffer).  All functional values are recorded first, printed at the end.
 * Run only through tools/hw/hwrun.sh. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
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
extern char b_ldw[], b_ocbpgap[], b_ocbwbgap[], b_sqld[], b_stld[], b_stldo[], b_chain[], b_ststream[], b_sq1[];
extern uint32_t p2_rd(uint32_t a);

enum { P_NONE = 0, P_OCBP = 1, P_TOUCH = 2, P_DIRTYV = 3, P_DIRTY = 4, P_CLEANV = 5, P_WARM = 6, P_OCBI = 7 };
static const uint8_t EVS[] = { 0x04, 0x05, 0x22, 0x25 };
static const char *EVN[] = { "rdmiss", "wrmiss", "ocfill", "frzoc" };
#define NEV (sizeof EVS / sizeof EVS[0])
#define REPS 5

static uint8_t big[0x40000] __attribute__((aligned(16384)));
static kdesc_t kd __attribute__((aligned(32)));
static int hot_idx[64], n_hot;

static uint32_t oca_rd(unsigned e) { return p2_rd(0xF4000000u | (e << 5)); }
static void ocbp(uint32_t a) { __asm__ volatile("ocbp @%0" :: "r"(a) : "memory"); }
static void ocbi(uint32_t a) { __asm__ volatile("ocbi @%0" :: "r"(a) : "memory"); }
static void movca(uint32_t a, uint32_t v) {
    register uint32_t r0 __asm__("r0") = v;
    __asm__ volatile("movca.l r0,@%0" :: "r"(a), "r"(r0) : "memory");
}

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
static void chain_build(uint32_t base, unsigned n, unsigned stride, unsigned woff, uint32_t orv) {
    for (unsigned i = 0; i < n; i++)
        R32(base + i * stride + woff) = ((base + ((i + 1) % n) * stride + woff) & 0x1FFFFFFF) | orv;
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
    /* a second load to each quadword of a line whose fill is in progress; first load at word 0 or word 4 */
    static const int offs0[] = { 4, 8, 12, 16, 20, 24, 28 };
    for (unsigned i = 0; i < 7; i++) {
        snprintf(nm, sizeof nm, "ldw w0 then +%d hit", offs0[i]);
        measure(nm, 64, P_TOUCH, R0, 64, 32, b_ldw, R0, 64, 32, offs0[i]);
        snprintf(nm, sizeof nm, "ldw w0 then +%d miss", offs0[i]);
        measure(nm, 64, P_OCBP, R0, 64, 32, b_ldw, R0, 64, 32, offs0[i]);
    }
    static const int offs4[] = { 4, 8, 12, -16, -12, -8, -4 };
    for (unsigned i = 0; i < 7; i++) {
        snprintf(nm, sizeof nm, "ldw w4 then %+d hit", offs4[i]);
        measure(nm, 64, P_TOUCH, R0, 64, 32, b_ldw, R0 + 16, 64, 32, (uint32_t)offs4[i]);
        snprintf(nm, sizeof nm, "ldw w4 then %+d miss", offs4[i]);
        measure(nm, 64, P_OCBP, R0, 64, 32, b_ldw, R0 + 16, 64, 32, (uint32_t)offs4[i]);
    }
    /* OCBP / OCBWB of dirty lines with a gap */
    for (uint32_t g = 1; g <= 16; g *= 2) {
        snprintf(nm, sizeof nm, "ocbpgap%lu clean", (unsigned long)g);
        measure(nm, 64, P_TOUCH, R0, 64, 32, b_ocbpgap, R0, 64, 32, g);
        snprintf(nm, sizeof nm, "ocbpgap%lu dirty", (unsigned long)g);
        measure(nm, 64, P_DIRTY, R0, 64, 32, b_ocbpgap, R0, 64, 32, g);
        snprintf(nm, sizeof nm, "ocbwbgap%lu dirty", (unsigned long)g);
        measure(nm, 64, P_DIRTY, R0, 64, 32, b_ocbwbgap, R0, 64, 32, g);
    }
    /* an OC load miss behind an SQ burst (QACR0 = area 3, destination: R0 + 8 KB) */
    {
        uint32_t q0 = R32(QACR0_A), q1 = R32(QACR1_A);
        R32(QACR0_A) = 0x0C; R32(QACR1_A) = 0x0C;
        uint32_t sq = 0xE0000000u | (P0(R0 + 8192) & 0x03FFFFC0);
        measure("sqld hit", 64, P_TOUCH, R0, 64, 32, b_sqld, R0, 64, 32, sq);
        measure("sqld miss", 64, P_OCBP, R0, 64, 32, b_sqld, R0, 64, 32, sq);
        measure("sq SQ0 only (ref)", 64, P_NONE, R0, 64, 32, b_sq1, sq, 64, 0, 0);
        R32(QACR0_A) = q0; R32(QACR1_A) = q1;
    }
    /* P2 store then P2 load: same line (w0 then w1) and next line */
    measure("P2 stld same line", 64, P_OCBP, R0, 128, 32, b_stld, P2(R0), 64, 32, 0);
    measure("P2 stldo other line", 64, P_OCBP, R0, 128, 32, b_stldo, P2(R0), 64, 32, 32);
    measure("P2 ststream s32 (ref)", 64, P_OCBP, R0, 128, 32, b_ststream, P2(R0), 64, 32, 0);
    chain_build(R0, 64, 32, 0, 0xA0000000);
    measure("P2 chain (ref)", 64, P_OCBP, R0, 64, 32, b_chain, P2(R0), 64, 0, 0);
}

/* ------------------------------------------------------------------------------------------------------------ */
static uint32_t rec[16][8], rent[16];
static const char *rname[16];
static int nrec;
static void snap(const char *nm, uint32_t line, unsigned e) {
    if (nrec >= 16) return;
    for (int k = 0; k < 8; k++) rec[nrec][k] = R32(line + 4 * k);
    rent[nrec] = oca_rd(e); rname[nrec] = nm; nrec++;
}
static void snapmem(const char *nm, uint32_t line) {
    if (nrec >= 16) return;
    for (int k = 0; k < 8; k++) rec[nrec][k] = R32(P2(line) + 4 * k);
    rent[nrec] = 0; rname[nrec] = nm; nrec++;
}
static void fill(uint32_t line, uint32_t base) { for (int k = 0; k < 8; k++) R32(P2(line) + 4 * k) = base + k; }

static void functional_tests(void) {
    uint32_t B = (uint32_t)big;
    set_hot();
    uint32_t X = pick_region(B + 0x10000, B + 0x20000, 1, 32), Y = X + 16384, e = OCIDX(X);
    irq_mask_t im = irq_disable();
    ocbp(X); ocbp(Y);
    /* a: dirty victim, MOVCA to w0 */
    fill(X, 0xC0DE0000); fill(Y, 0x5A5A0000);
    (void)R32(X); R32(X + 8) = 0x7777000C; movca(Y, 0x9999AAAA);
    snap("a dirty victim, MOVCA w0: Y", Y, e);
    ocbp(Y);
    snapmem("a  ... after OCBP Y: memory Y", Y);
    snapmem("a  ... memory X (written back)", X);
    /* b: dirty victim, MOVCA to w5 */
    fill(X, 0xC1DE0000); fill(Y, 0x5B5B0000);
    (void)R32(X); R32(X + 12) = 0x6666000D; movca(Y + 20, 0x8888BBBB);
    snap("b dirty victim, MOVCA w5: Y", Y, e);
    ocbi(Y);
    /* c: clean victim, MOVCA to w5 */
    fill(X, 0xC2DE0000); fill(Y, 0x5C5C0000);
    (void)R32(X); movca(Y + 20, 0x7777CCCC);
    snap("c clean victim, MOVCA w5: Y", Y, e);
    ocbi(Y);
    /* d: MOVCA hit (Y valid) to w2 */
    fill(Y, 0x5D5D0000);
    (void)R32(Y); movca(Y + 8, 0x6666DDDD);
    snap("d MOVCA hit w2: Y", Y, e);
    ocbp(Y);
    /* e: plain store hit to w2 (control) */
    fill(Y, 0x5E5E0000);
    (void)R32(Y); R32(Y + 8) = 0x5555EEEE;
    snap("e store hit w2: Y", Y, e);
    ocbp(Y);
    /* f: plain store miss with a dirty victim (control) */
    fill(X, 0xC3DE0000); fill(Y, 0x5F5F0000);
    (void)R32(X); R32(X + 8) = 0x4444000E; R32(Y) = 0x3333FFFF;
    snap("f store miss w0, dirty victim: Y", Y, e);
    ocbp(Y);
    /* g: dirty victim, MOVCA w0, a second MOVCA w3 (hit) */
    fill(X, 0xC4DE0000); fill(Y, 0x5A5B0000);
    (void)R32(X); R32(X) = 0x2222000F; movca(Y, 0x1111AAAA); movca(Y + 12, 0x1111BBBB);
    snap("g dirty victim MOVCA w0 + MOVCA w3: Y", Y, e);
    ocbi(Y);
    /* h: dirty victim is a line written only through MOVCA (dirty, never filled) */
    fill(X, 0xC5DE0000); fill(Y, 0x5A5C0000);
    ocbi(X); movca(X, 0x0A0A0A0A); movca(Y + 4, 0x0B0B0B0B);
    snap("h MOVCA-dirty victim, MOVCA w1: Y", Y, e);
    ocbi(Y);
    irq_restore(im);
    for (int i = 0; i < nrec; i++) {
        printf("F %-40s entry %08lx:", rname[i], (unsigned long)rent[i]);
        for (int k = 0; k < 8; k++) printf(" %08lx", (unsigned long)rec[i][k]);
        printf("\n");
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("cachetest3: CCR %08lx\n", (unsigned long)R32(CCR_A));
    functional_tests();
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    timing_tests();
    perf_cntr_stop(PRFC0); perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    printf("cachetest3 DONE\n");
    return 0;
}
