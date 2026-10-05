/* c1e.c -- soc-design probe C1, run 5 (rtl/v1/SOC.md): exception entry cost against the distance from an earlier
 * on-chip register access.  Loops "ACC ; K adds ; X ; dt ; bf" against the BRA reference (c1d_asm.S); the handler
 * has no register access.  E = L(X) - L(ref) + 2.  Results printed at the end.
 * Safety: interrupts masked; PVR only read; BARA rewritten with its own value (the UBC channels stay disabled). */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <dc/perfctr.h>

typedef void (*tfn)(void);
extern void c1_call(tfn fn, void *vbr, uint32_t skip, uint32_t r5val);
extern uint8_t c1_vt[], c1_vtb[], c1_base[], c1_code_end[];
uint32_t c1_ctx[32] __attribute__((aligned(32)));
volatile uint32_t c1_budget, c1_aborted, c1_n, c1_ts[4];
uint8_t c1_scratch[64] __attribute__((aligned(32)));

#define F(x) extern void x(void);
#define FK(a, k) F(tl_##a##k##_ill) F(tl_##a##k##_dae) F(tl_##a##k##_ref)
FK(m, 0) FK(m, 1) FK(m, 2) FK(m, 4) FK(m, 6) FK(M, 0) FK(M, 2) FK(M, 4) FK(M, 6) FK(P, 0) FK(P, 2) FK(P, 4)
FK(c, 2)
#undef F

static uint32_t get_sr(void) { uint32_t v; __asm__ volatile("stc sr,%0" : "=r"(v)); return v; }
#define N1 16
#define N2 272
static int abt;
static void *cur_vt;
static double run_t(tfn fn, uint32_t skip, double *mx) {
    uint64_t b1 = ~0ull, b2 = ~0ull, w2 = 0;
    for (int rep = 0; rep < 5; rep++)
        for (int w = 0; w < 2; w++) {
            uint32_t n = w ? N2 : N1;
            c1_n = n;
            irq_mask_t im = irq_disable();
            c1_budget = n + 8; c1_aborted = 0;
            uint64_t a = perf_cntr_count(PRFC1);
            c1_call(fn, cur_vt, skip, (uint32_t)c1_ts);
            uint64_t b = perf_cntr_count(PRFC1);
            irq_restore(im);
            abt |= c1_aborted;
            uint64_t d = b - a;
            if (w) { if (d < b2) b2 = d; if (d > w2) w2 = d; }
            else if (d < b1) b1 = d;
        }
    *mx = (double)(int64_t)(w2 - b1) / (N2 - N1);
    return (double)(int64_t)(b2 - b1) / (N2 - N1);
}

struct row { const char *name; tfn ill, dae, ref; double l[3], m[3]; };
#define R(a, k) { #a #k, tl_##a##k##_ill, tl_##a##k##_dae, tl_##a##k##_ref, {0}, {0} }
static struct row rows[] = { R(m, 0), R(m, 1), R(m, 2), R(m, 4), R(m, 6), R(M, 0), R(M, 2), R(M, 4), R(M, 6),
                             R(P, 0), R(P, 2), R(P, 4), R(c, 2) };

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("c1e: sr %08lx base %p end %p bara %08lx\n", (unsigned long)get_sr(), (void *)c1_base,
           (void *)c1_code_end, (unsigned long)*(volatile uint32_t *)0xFF200000);
    if (get_sr() & 0x30000000) { printf("c1e: SR.BL or SR.RB set, nothing run\n"); return 1; }
    for (int i = 0; i < 16; i++) ((volatile uint32_t *)c1_scratch)[i] = 0x3F800000;
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    int nr = sizeof rows / sizeof rows[0];
    for (int i = 0; i < nr; i++) {
        cur_vt = (rows[i].name[0] == 'M' || rows[i].name[0] == 'P') ? (void *)c1_vtb : (void *)c1_vt;
        rows[i].l[0] = run_t(rows[i].ill, 2, &rows[i].m[0]);
        rows[i].l[1] = run_t(rows[i].dae, 2, &rows[i].m[1]);
        rows[i].l[2] = run_t(rows[i].ref, 0, &rows[i].m[2]);
    }
    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    for (int i = 0; i < nr; i++)
        printf("%-4s ill %7.3f (%7.3f) dae %7.3f (%7.3f) ref %7.3f (%7.3f)  E ill %6.3f dae %6.3f\n", rows[i].name,
               rows[i].l[0], rows[i].m[0], rows[i].l[1], rows[i].m[1], rows[i].l[2], rows[i].m[2],
               rows[i].l[0] - rows[i].l[2] + 2, rows[i].l[1] - rows[i].l[2] + 2);
    printf("c1e: done aborted %d bara %08lx\n", abt, (unsigned long)*(volatile uint32_t *)0xFF200000);
    return 0;
}
