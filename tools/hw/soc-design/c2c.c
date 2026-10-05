/* c2c.c -- soc-design probe C2, run 4: which fetch-side events an array read in a loop causes (rtl/v1/SOC.md 3.1.5).
 * PRFC1 counts elapsed cycles, PRFC0 one event per pass; per-iteration values = (N2 - N1 difference) / (N2 - N1).
 * Loops from c2_asm.S: x1 / xk2 / xk8 (one read, 0 / 2 / 8 fillers), g0 (two back to back), x8.  Reads only. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <dc/perfctr.h>

typedef void (*tfn)(uint32_t n, uint32_t a, uint32_t b, uint32_t c);
#define D4(x) extern void x(uint32_t, uint32_t, uint32_t, uint32_t);
D4(c2_x1) D4(c2_xk2) D4(c2_xk8) D4(c2_g0) D4(c2_x8)
uint32_t c2_scratch[16] __attribute__((aligned(32)));
#define N1 16
#define N2 272

static void run(tfn fn, uint32_t a, int ev, double *cyc, double *cnt) {
    uint64_t c1 = ~0ull, c2 = ~0ull, e1 = 0, e2 = 0;
    perf_cntr_stop(PRFC0);
    perf_cntr_clear(PRFC0);
    perf_cntr_start(PRFC0, ev, PMCR_COUNT_CPU_CYCLES);
    for (int rep = 0; rep < 5; rep++)
        for (int w = 0; w < 2; w++) {
            uint32_t n = w ? N2 : N1;
            irq_mask_t im = irq_disable();
            uint64_t s = perf_cntr_count(PRFC1), es = perf_cntr_count(PRFC0);
            fn(n, a, 0, (uint32_t)c2_scratch);
            uint64_t e = perf_cntr_count(PRFC1), ee = perf_cntr_count(PRFC0);
            irq_restore(im);
            if (w) { if (e - s < c2) { c2 = e - s; e2 = ee - es; } }
            else if (e - s < c1) { c1 = e - s; e1 = ee - es; }
        }
    *cyc = (double)(int64_t)(c2 - c1) / (N2 - N1);
    *cnt = (double)(int64_t)(e2 - e1) / (N2 - N1);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    struct { const char *name; uint32_t a; } T[] = {
        { "expevt", 0xFF000024u }, { "ocaa", 0xF4000000u }, { "utlbaa", 0xF6000000u },
    };
    struct { const char *name; tfn fn; } L[] = {
        { "x1", c2_x1 }, { "xk2", c2_xk2 }, { "xk8", c2_xk8 }, { "g0", c2_g0 }, { "x8", c2_x8 },
    };
    static const int EV[] = { 0x06, 0x0A, 0x08, 0x21, 0x24, 0x25, 0x27, 0x13, 0x0D, 0x09 };
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    printf("c2c: events");
    for (unsigned e = 0; e < sizeof EV / sizeof EV[0]; e++) printf("   0x%02x", EV[e]);
    printf("\n");
    for (int t = 0; t < 3; t++)
        for (unsigned l = 0; l < sizeof L / sizeof L[0]; l++) {
            double cyc = 0, cnt;
            printf("E %-7s %-4s", T[t].name, L[l].name);
            for (unsigned e = 0; e < sizeof EV / sizeof EV[0]; e++) {
                run(L[l].fn, T[t].a, EV[e], &cyc, &cnt);
                printf(" %6.2f", cnt);
            }
            printf("   cyc %6.2f\n", cyc);
            fflush(stdout);
        }
    perf_cntr_stop(PRFC0);
    perf_cntr_stop(PRFC1);
    perf_cntr_clear(PRFC0);
    perf_cntr_timer_enable();
    printf("c2c: done\n");
    return 0;
}
