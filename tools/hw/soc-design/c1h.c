/* c1h.c -- soc-design probe C1, run 8 (rtl/v1/SOC.md): the instruction after the exception (c1h_asm.S).
 * Per form: loop period of X = H'FFFD / misaligned MOV.L / the BRA reference, and the handler's B - A of the last
 * iteration.  Results printed at the end.  Safety: interrupts masked, budgets, every register restored. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <dc/perfctr.h>

typedef void (*tfn)(void);
extern void c1_call(tfn fn, void *vbr, uint32_t skip, uint32_t r5val);
extern uint8_t c1_vt[], c1_base[], c1_code_end[];
uint32_t c1_ctx[32] __attribute__((aligned(32)));
volatile uint32_t c1_budget, c1_aborted, c1_n, c1_ts[300];
volatile uint32_t c1_out[300];
uint8_t c1_scratch[64] __attribute__((aligned(32)));

#define F(x) extern void x(void);
#define FF(f) F(ty_##f##_ill) F(ty_##f##_trap) F(ty_##f##_ref)
FF(st) FF(ld) FF(fm) FF(mv) FF(ad) FF(bt) FF(no)
#undef F

static uint32_t get_sr(void) { uint32_t v; __asm__ volatile("stc sr,%0" : "=r"(v)); return v; }
#define N1 16
#define N2 272
static int abt;
static double run_t(tfn fn, uint32_t skip, double *mx, long *br) {
    uint64_t b1 = ~0ull, b2 = ~0ull, w2 = 0;
    for (int rep = 0; rep < 5; rep++)
        for (int w = 0; w < 2; w++) {
            uint32_t n = w ? N2 : N1;
            c1_n = n; for (int q = 0; q < 12; q++) c1_ts[q] = 0;
            irq_mask_t im = irq_disable();
            c1_budget = n + 8; c1_aborted = 0;
            uint64_t a = perf_cntr_count(PRFC1);
            c1_call(fn, c1_vt, skip, (uint32_t)c1_ts);
            uint64_t b = perf_cntr_count(PRFC1);
            irq_restore(im);
            abt |= c1_aborted;
            if (rep == 0 && w == 0) for (int q = 0; q < 12; q++) br[q] = (long)c1_ts[q];
            uint64_t d = b - a;
            if (w) { if (d < b2) b2 = d; if (d > w2) w2 = d; }
            else if (d < b1) b1 = d;
        }
    *mx = (double)(int64_t)(w2 - b1) / (N2 - N1);
    return (double)(int64_t)(b2 - b1) / (N2 - N1);
}
struct row { const char *name; tfn f[3]; double l[3], m[3]; long br[3][12]; };
#define R(f) { #f, { ty_##f##_ill, ty_##f##_trap, ty_##f##_ref }, {0}, {0}, {{0}} }
static struct row rows[] = { R(st), R(ld), R(fm), R(mv), R(ad), R(bt), R(no) };
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("c1h: sr %08lx base %p end %p\n", (unsigned long)get_sr(), (void *)c1_base, (void *)c1_code_end);
    if (get_sr() & 0x30000000) { printf("c1h: SR.BL or SR.RB set, nothing run\n"); return 1; }
    for (int i = 0; i < 16; i++) ((volatile uint32_t *)c1_scratch)[i] = 0x3F800000;
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    int nr = sizeof rows / sizeof rows[0];
    static const uint32_t skip[3] = { 2, 0, 0 };
    for (int i = 0; i < nr; i++)
        for (int k = 0; k < 3; k++) rows[i].l[k] = run_t(rows[i].f[k], skip[k], &rows[i].m[k], rows[i].br[k]);
    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    static const char *cn[3] = { "ill", "trap", "ref" };
    for (int i = 0; i < nr; i++) {
        printf("%-3s L ill %7.3f trap %7.3f ref %7.3f (max %7.3f %7.3f %7.3f)  E ill %6.3f trap %6.3f\n", rows[i].name,
               rows[i].l[0], rows[i].l[1], rows[i].l[2], rows[i].m[0], rows[i].m[1], rows[i].m[2],
               rows[i].l[0] - rows[i].l[2] + 2, rows[i].l[1] - rows[i].l[2] + 2);
        for (int k = 0; k < 3; k++) {
            printf("    B-A %s:", cn[k]);
            for (int r = 0; r < 12; r++) printf(" %ld", rows[i].br[k][r]);
            printf("\n");
        }
    }
    printf("c1h: done aborted %d\n", abt);
    return 0;
}
