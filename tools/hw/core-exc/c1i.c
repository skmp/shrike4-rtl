/* c1i.c -- core-exc probe c1i (WI-CORE-A step A2), run 9 of the C1 entry-timing series (rtl/v1/SOC.md 9.1, 9.16);
 * derived from tools/hw/soc-design/c1h.c.  c1i_asm.S describes the loops.
 * Per row: the loop period of each form (X = H'FFFD / TRAPA / the BRA reference, or for the UBC rows the data break /
 * the operand-address break / the reference) and the handler's record of the last iteration of the first N1 = 16 pass
 * (B - A; the UBC rows: SSR.T, SPC relative to the loop function, B - A).  Safety: interrupts masked, budgets, every
 * register restored, the UBC cleared after each call (KOS's state: all zero). */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <dc/perfctr.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R8(a)  (*(volatile uint8_t *)(uintptr_t)(a))
#define BARA  0xFF200000u
#define BAMRA 0xFF200004u
#define BBRA  0xFF200008u
#define BARB  0xFF20000Cu
#define BAMRB 0xFF200010u
#define BBRB  0xFF200014u
#define BDRB  0xFF200018u
#define BDMRB 0xFF20001Cu
#define BRCR  0xFF200020u
#define A(x) ((uint32_t)(uintptr_t)(x))

typedef void (*tfn)(void);
extern void c1_call(tfn fn, void *vbr, uint32_t skip, uint32_t r5val);
extern void c1_ubc_wait(void);
extern uint8_t c1_base[], c1_code_end[];
extern uint8_t c1_h_h0[], c1_h_n[], c1_h_spc[], c1_h_gbr[], c1_h_vbr[], c1_h_dbr[], c1_h_pr[], c1_h_gm[], c1_h_rts[],
    c1_h_ssr[];
uint32_t c1_ctx[32] __attribute__((aligned(32)));
volatile uint32_t c1_budget, c1_aborted, c1_n, c1_ts[16] __attribute__((aligned(32)));
volatile uint32_t c1_out[64] __attribute__((aligned(32)));
uint8_t c1_scratch[64] __attribute__((aligned(32)));

#define F(x) extern void x(void);
#define FY(y, h) F(ty_##y##_##h##_ill) F(ty_##y##_##h##_trap) F(ty_##y##_##h##_ref)
#define FU(y) F(ty_##y##_ssr_brk) F(ty_##y##_ssr_op) F(ty_##y##_ssr_uref)
FY(ad, h0) FY(ld, h0) FY(lp, h0) FY(pf, h0) FY(ob, h0) FY(fs, h0) FY(st, h0) FY(l2, h0) FY(l3, h0) FY(p2, h0)
FY(f2, h0) FY(ad, n) FY(ld, n) FY(ad, spc) FY(ld, spc) FY(st, spc) FY(ad, gbr) FY(ad, vbr) FY(ad, dbr) FY(ad, pr)
FY(ad, gm) FY(ad, rts) FU(un) FU(up)
#undef F

static uint32_t get_sr(void) { uint32_t v; __asm__ volatile("stc sr,%0" : "=r"(v)); return v; }

/* the UBC around one call: 0 off, 1 channel B data break on the scratch word, 2 channel A operand-address break */
static void ubc_set(int mode) {
    R16(BBRA) = 0; R16(BBRB) = 0;
    if (mode == 1) {
        R32(BARB) = A(c1_scratch); R8(BAMRB) = 0x04; R32(BDRB) = 0x3F800000u; R32(BDMRB) = 0;
        R16(BRCR) = 0x0080; R16(BBRB) = 0x0027;
    } else if (mode == 2) {
        R32(BARA) = A(c1_scratch); R8(BAMRA) = 0x04; R16(BRCR) = 0; R16(BBRA) = 0x0024;
    } else {
        R16(BRCR) = 0; R32(BARA) = 0; R8(BAMRA) = 0; R32(BARB) = 0; R8(BAMRB) = 0; R32(BDRB) = 0; R32(BDMRB) = 0;
    }
    c1_ubc_wait();
}

#define N1 16
#define N2 272
static int abt;
static double run_t(tfn fn, void *h, uint32_t skip, int ubc, double *mx, long *br, int nbr) {
    uint64_t b1 = ~0ull, b2 = ~0ull, w2 = 0;
    for (int rep = 0; rep < 5; rep++)
        for (int w = 0; w < 2; w++) {
            uint32_t n = w ? N2 : N1;
            c1_n = n; for (int q = 0; q < nbr; q++) c1_ts[q] = 0;
            irq_mask_t im = irq_disable();
            c1_budget = n + 8; c1_aborted = 0;
            if (ubc) ubc_set(ubc);
            uint64_t a = perf_cntr_count(PRFC1);
            c1_call(fn, (uint8_t *)h - 0x100, skip, (uint32_t)c1_ts);
            uint64_t b = perf_cntr_count(PRFC1);
            if (ubc) ubc_set(0);
            irq_restore(im);
            abt |= c1_aborted;
            if (rep == 0 && w == 0) for (int q = 0; q < nbr; q++) br[q] = (long)c1_ts[q];
            uint64_t d = b - a;
            if (w) { if (d < b2) b2 = d; if (d > w2) w2 = d; }
            else if (d < b1) b1 = d;
        }
    *mx = (double)(int64_t)(w2 - b1) / (N2 - N1);
    return (double)(int64_t)(b2 - b1) / (N2 - N1);
}

struct row { const char *name; void *h; int ubc; tfn f[3]; double l[3], m[3]; long br[3][3]; };
#define R(y, h) { #y "_" #h, c1_h_##h, 0, { ty_##y##_##h##_ill, ty_##y##_##h##_trap, ty_##y##_##h##_ref }, {0}, {0}, {{0}} }
#define U(y) { #y "_ssr", c1_h_ssr, 1, { ty_##y##_ssr_brk, ty_##y##_ssr_op, ty_##y##_ssr_uref }, {0}, {0}, {{0}} }
static struct row rows[] = {
    R(ad, h0), R(ld, h0), R(lp, h0), R(pf, h0), R(ob, h0), R(fs, h0), R(st, h0), R(l2, h0), R(l3, h0), R(p2, h0),
    R(f2, h0), R(ad, n), R(ld, n), R(ad, spc), R(ld, spc), R(st, spc), R(ad, gbr), R(ad, vbr), R(ad, dbr), R(ad, pr),
    R(ad, gm), R(ad, rts), U(un), U(up),
};

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("c1i: sr %08lx base %p end %p\n", (unsigned long)get_sr(), (void *)c1_base, (void *)c1_code_end);
    if (get_sr() & 0x30000000) { printf("c1i: SR.BL or SR.RB set, nothing run\n"); return 1; }
    for (int i = 0; i < 16; i++) ((volatile uint32_t *)c1_scratch)[i] = 0x3F800000;
    for (int i = 0; i < 16; i++) c1_out[i] = 0x3F800000;
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    int nr = sizeof rows / sizeof rows[0];
    static const uint32_t skip[3] = { 2, 0, 0 };
    for (int i = 0; i < nr; i++)
        for (int k = 0; k < 3; k++) {
            const int ubc = rows[i].ubc && k < 2 ? k + 1 : 0;
            rows[i].l[k] = run_t(rows[i].f[k], rows[i].h, rows[i].ubc ? 0 : skip[k], ubc, &rows[i].m[k], rows[i].br[k],
                                 3);
        }
    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    static const char *cn[2][3] = { { "ill", "trap", "ref" }, { "brk", "op", "ref" } };
    for (int i = 0; i < nr; i++) {
        const int u = rows[i].ubc;
        printf("%-7s L %s %7.3f %s %7.3f %s %7.3f (max %7.3f %7.3f %7.3f)  E %s %6.3f %s %6.3f\n", rows[i].name,
               cn[u][0], rows[i].l[0], cn[u][1], rows[i].l[1], cn[u][2], rows[i].l[2], rows[i].m[0], rows[i].m[1],
               rows[i].m[2], cn[u][0], rows[i].l[0] - rows[i].l[2] + 2, cn[u][1], rows[i].l[1] - rows[i].l[2] + 2);
        if (!u) {
            printf("    B-A");
            for (int k = 0; k < 3; k++) printf(" %s %ld", cn[u][k], rows[i].br[k][0]);
        } else {
            printf("    T/spc/B-A");
            for (int k = 0; k < 3; k++)
                printf(" %s %ld/%ld/%ld", cn[u][k], rows[i].br[k][1] & 1,
                       (long)((uint32_t)rows[i].br[k][2] - A(rows[i].f[k])), rows[i].br[k][0]);
        }
        printf("\n");
    }
    printf("c1i: done aborted %d\n", abt);
    return 0;
}
