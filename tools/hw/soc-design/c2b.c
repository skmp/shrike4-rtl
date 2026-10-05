/* c2b.c -- soc-design probe C2, run 3: where the extra cost of a P4 array read goes (rtl/v1/SOC.md 3.1.5).
 *   bkK   PMCTR ; X ; K EX fillers ; PMCTR   (straight code)      xkK   loop "X ; K EX fillers ; dt ; bf"
 * X = EXPEVT (Iclk register), a cached RAM word, the OC address array, the UTLB address array.  Reads only. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <dc/perfctr.h>

typedef void (*tfn)(uint32_t n, uint32_t a, uint32_t b, uint32_t c);
typedef uint32_t (*bfn)(uint32_t pm, uint32_t a);
#define D4(x) extern void x(uint32_t, uint32_t, uint32_t, uint32_t);
#define D2(x) extern uint32_t x(uint32_t, uint32_t);
D4(c2_x1) D4(c2_xk1) D4(c2_xk2) D4(c2_xk3) D4(c2_xk4) D4(c2_xk6) D4(c2_xk8) D4(c2_xk12) D4(c2_xk16) D4(c2_xk24)
D2(c2_bk0) D2(c2_bk1) D2(c2_bk2) D2(c2_bk3) D2(c2_bk4) D2(c2_bk6) D2(c2_bk8) D2(c2_bk12) D2(c2_bk16) D2(c2_bk24)

uint32_t c2_scratch[16] __attribute__((aligned(32)));
#define PMCTR2L 0xFF100010u
#define N1 16
#define N2 272

static double run_t(tfn fn, uint32_t a) {
    uint64_t b1 = ~0ull, b2 = ~0ull;
    for (int rep = 0; rep < 5; rep++)
        for (int w = 0; w < 2; w++) {
            uint32_t n = w ? N2 : N1;
            irq_mask_t im = irq_disable();
            uint64_t s = perf_cntr_count(PRFC1);
            fn(n, a, 0, (uint32_t)c2_scratch);
            uint64_t e = perf_cntr_count(PRFC1);
            irq_restore(im);
            uint64_t d = e - s;
            if (w) { if (d < b2) b2 = d; } else if (d < b1) b1 = d;
        }
    return (double)(int64_t)(b2 - b1) / (N2 - N1);
}
static uint32_t run_b(bfn fn, uint32_t a) {
    uint32_t best = 0xFFFFFFFFu;
    for (int rep = 0; rep < 16; rep++) {
        irq_mask_t im = irq_disable();
        uint32_t d = fn(PMCTR2L, a);
        irq_restore(im);
        if (d < best) best = d;
    }
    return best;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    struct { const char *name; uint32_t a; } T[] = {
        { "expevt", 0xFF000024u }, { "ram", (uint32_t)c2_scratch }, { "ocaa", 0xF4000000u }, { "utlbaa", 0xF6000000u },
    };
    static const int K[10] = { 0, 1, 2, 3, 4, 6, 8, 12, 16, 24 };
    bfn B[10] = { c2_bk0, c2_bk1, c2_bk2, c2_bk3, c2_bk4, c2_bk6, c2_bk8, c2_bk12, c2_bk16, c2_bk24 };
    tfn L[10] = { c2_x1, c2_xk1, c2_xk2, c2_xk3, c2_xk4, c2_xk6, c2_xk8, c2_xk12, c2_xk16, c2_xk24 };
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    printf("c2b: K         ");
    for (int k = 0; k < 10; k++) printf(" %6d", K[k]);
    printf("\n");
    for (int t = 0; t < 4; t++) {
        printf("B %-7s bk  ", T[t].name);
        for (int k = 0; k < 10; k++) printf(" %6lu", (unsigned long)run_b(B[k], T[t].a));
        printf("\nL %-7s xk  ", T[t].name);
        for (int k = 0; k < 10; k++) printf(" %6.2f", run_t(L[k], T[t].a));
        printf("\n");
        fflush(stdout);
    }
    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    printf("c2b: done\n");
    return 0;
}
