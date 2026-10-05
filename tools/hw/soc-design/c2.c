/* c2.c -- soc-design probe C2 (rtl/v1/SOC.md section 3.1.5): the structure behind the cost of a P4 cache / TLB
 * array read.  Loops (cycles per iteration = (min over 5 runs of N2 - N1 iterations) / (N2 - N1), PMCTR2 in elapsed
 * cycle mode, interrupts masked) and PMCTR brackets.  Reads only; the asm is c2_asm.S.
 *   x8   8 reads per iteration                       x1   1 read per iteration
 *   gK   read ; K fillers ; read   (K = 0..6)          alt  A ; B, 4 pairs per iteration
 *   sp4  4 reads with 4 fillers after each             br1 / br2  PMCTR ; 1 / 2 reads ; PMCTR (br0: none) */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <dc/perfctr.h>

typedef void (*tfn)(uint32_t n, uint32_t a, uint32_t b, uint32_t c);
typedef uint32_t (*bfn)(uint32_t pm, uint32_t a);
extern void c2_x8(uint32_t, uint32_t, uint32_t, uint32_t), c2_x1(uint32_t, uint32_t, uint32_t, uint32_t),
    c2_alt(uint32_t, uint32_t, uint32_t, uint32_t), c2_sp4(uint32_t, uint32_t, uint32_t, uint32_t);
extern void c2_g0(uint32_t, uint32_t, uint32_t, uint32_t), c2_g1(uint32_t, uint32_t, uint32_t, uint32_t),
    c2_g2(uint32_t, uint32_t, uint32_t, uint32_t), c2_g3(uint32_t, uint32_t, uint32_t, uint32_t),
    c2_g4(uint32_t, uint32_t, uint32_t, uint32_t), c2_g5(uint32_t, uint32_t, uint32_t, uint32_t),
    c2_g6(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t c2_br0(uint32_t, uint32_t), c2_br1(uint32_t, uint32_t), c2_br2(uint32_t, uint32_t);

uint32_t c2_scratch[16] __attribute__((aligned(32)));

#define PMCTR2L 0xFF100010u
#define N1 16
#define N2 272

static double run_t(tfn fn, uint32_t a, uint32_t b) {
    uint64_t b1 = ~0ull, b2 = ~0ull;
    uint32_t c = (uint32_t)c2_scratch;
    for (int rep = 0; rep < 5; rep++)
        for (int w = 0; w < 2; w++) {
            uint32_t n = w ? N2 : N1;
            irq_mask_t im = irq_disable();
            uint64_t s = perf_cntr_count(PRFC1);
            fn(n, a, b, c);
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

struct tgt { const char *name; uint32_t a; };

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t scr = (uint32_t)c2_scratch;
    /* run 1 read the IC address array (0xF0000000) from P1 in these loops and crashed (a jump to PC 5 after the
     * loop): the IC arrays share the instruction-fetch port.  Run 2 keeps to the data-side arrays (OC, UTLB), which
     * core-integration run 1 read in the same x8 shape without harm. */
    struct tgt T[] = {
        { "expevt", 0xFF000024u }, { "ram", scr },
        { "ocaa", 0xF4000000u }, { "ocda", 0xF5000000u }, { "utlbaa", 0xF6000000u }, { "utlbda", 0xF7000000u },
    };
    const int NT = sizeof T / sizeof T[0];
    tfn G[7] = { c2_g0, c2_g1, c2_g2, c2_g3, c2_g4, c2_g5, c2_g6 };

    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    printf("c2: scratch %08lx\n", (unsigned long)scr);

    for (int t = 0; t < NT; t++) {
        printf("L %-7s x8 %7.3f  x1 %6.3f  sp4 %6.3f  g0-6", T[t].name, run_t(c2_x8, T[t].a, 0),
               run_t(c2_x1, T[t].a, 0), run_t(c2_sp4, T[t].a, 0));
        for (int k = 0; k < 7; k++) printf(" %6.3f", run_t(G[k], T[t].a, 0));
        printf("\n");
        fflush(stdout);
    }
    /* A ; B alternation: one shared port, or one port per array? */
    static const int AB[][2] = { {2, 3}, {2, 4}, {4, 2}, {2, 5}, {3, 4}, {2, 0}, {0, 2}, {4, 0}, {0, 4},
                                 {2, 1}, {1, 2}, {4, 1}, {1, 4} };
    for (unsigned i = 0; i < sizeof AB / sizeof AB[0]; i++) {
        const struct tgt *x = &T[AB[i][0]], *y = &T[AB[i][1]];
        printf("A %-7s %-7s alt %7.3f\n", x->name, y->name, run_t(c2_alt, x->a, y->a));
    }
    fflush(stdout);
    printf("B br0 %lu\n", (unsigned long)run_b(c2_br0, 0));
    for (int t = 0; t < NT; t++)
        printf("B %-7s br1 %lu br2 %lu\n", T[t].name, (unsigned long)run_b(c2_br1, T[t].a),
               (unsigned long)run_b(c2_br2, T[t].a));

    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    printf("c2: done\n");
    return 0;
}
