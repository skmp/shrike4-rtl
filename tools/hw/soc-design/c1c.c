/* c1c.c -- soc-design probe C1, run 3 (rtl/v1/SOC.md): which handler instructions are cheaper after an exception
 * entry than after a BRA.  Seven handler variants (c1c_asm.S), each timed as a loop (X = H'FFFD against the BRA
 * reference) and as a b3 bracket with up to three PMCTR2L stamps inside the handler.  Results printed at the end.
 * Safety: interrupts masked; BCR1 / TCOR0 only read; BARA rewritten with its own value (UBC channels stay disabled). */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <dc/perfctr.h>

typedef void (*tfn)(void);
extern void c1_call(tfn fn, void *vbr, uint32_t skip, uint32_t r5val);
extern uint8_t c1_vt0[], c1_vt1[], c1_vt2[], c1_vt3[], c1_vt4[], c1_vt5[], c1_vt6[], c1_base[], c1_code_end[];

uint32_t c1_ctx[32] __attribute__((aligned(32)));
volatile uint32_t c1_budget, c1_aborted, c1_n, c1_r4, c1_r6;
volatile uint32_t c1_ts[4] __attribute__((aligned(32)));
volatile uint32_t c1_out[16] __attribute__((aligned(32)));
uint8_t c1_scratch[64] __attribute__((aligned(32)));

#define F(x) extern void x(void);
#define FV(v) F(tl_v##v##_ill) F(tl_v##v##_ref) F(tb_v##v##_ill) F(tb_v##v##_ref)
FV(0) FV(1) FV(2) FV(3) FV(4) FV(5) FV(6)
#undef F

static uint32_t get_sr(void) { uint32_t v; __asm__ volatile("stc sr,%0" : "=r"(v)); return v; }

#define N1 16
#define N2 272
static double lper[7][2], lmax[7][2];
static long bst[7][2][6][3];
static int abt;

static void run_t(int v, int k, tfn fn, void *vbr, uint32_t skip) {
    uint64_t b1 = ~0ull, b2 = ~0ull, w2 = 0;
    for (int rep = 0; rep < 5; rep++)
        for (int w = 0; w < 2; w++) {
            uint32_t n = w ? N2 : N1;
            c1_n = n;
            irq_mask_t im = irq_disable();
            c1_budget = 0; c1_aborted = 0;
            uint64_t a = perf_cntr_count(PRFC1);
            c1_call(fn, vbr, skip, (uint32_t)c1_ts);
            uint64_t b = perf_cntr_count(PRFC1);
            irq_restore(im);
            abt |= c1_aborted;
            uint64_t d = b - a;
            if (w) { if (d < b2) b2 = d; if (d > w2) w2 = d; }
            else if (d < b1) b1 = d;
        }
    lper[v][k] = (double)(int64_t)(b2 - b1) / (N2 - N1);
    lmax[v][k] = (double)(int64_t)(w2 - b1) / (N2 - N1);
}

static void run_b(int v, int k, tfn fn, void *vbr, uint32_t skip) {
    for (int r = 0; r < 6; r++) {
        c1_ts[0] = c1_ts[1] = c1_ts[2] = 0; c1_out[0] = 0;
        irq_mask_t im = irq_disable();
        c1_budget = 0; c1_aborted = 0;
        c1_call(fn, vbr, skip, (uint32_t)c1_ts);
        irq_restore(im);
        abt |= c1_aborted;
        bst[v][k][r][0] = (long)(c1_ts[0] - c1_out[0]);
        bst[v][k][r][1] = (long)(c1_ts[1] - c1_ts[0]);
        bst[v][k][r][2] = (long)(c1_ts[2] - c1_ts[1]);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    static const char *vn[7] = { "B1;B2;B3", "8nop;B1;B2", "B1;BCR1;B2", "B1;TCOR0;B2", "B1;ld;B2", "B1;stcspc;B2",
                                 "B1;BARA=;B2" };
    uint8_t *vt[7] = { c1_vt0, c1_vt1, c1_vt2, c1_vt3, c1_vt4, c1_vt5, c1_vt6 };
    tfn tli[7] = { tl_v0_ill, tl_v1_ill, tl_v2_ill, tl_v3_ill, tl_v4_ill, tl_v5_ill, tl_v6_ill };
    tfn tlr[7] = { tl_v0_ref, tl_v1_ref, tl_v2_ref, tl_v3_ref, tl_v4_ref, tl_v5_ref, tl_v6_ref };
    tfn tbi[7] = { tb_v0_ill, tb_v1_ill, tb_v2_ill, tb_v3_ill, tb_v4_ill, tb_v5_ill, tb_v6_ill };
    tfn tbr[7] = { tb_v0_ref, tb_v1_ref, tb_v2_ref, tb_v3_ref, tb_v4_ref, tb_v5_ref, tb_v6_ref };
    uint32_t r4[7] = { 0, 0, 0xFF800000, 0xFFD80008, 0, 0, 0xFF200000 };
    printf("c1c: sr %08lx base %p end %p scratch %p bara %08lx\n", (unsigned long)get_sr(), (void *)c1_base,
           (void *)c1_code_end, (void *)c1_scratch, (unsigned long)*(volatile uint32_t *)0xFF200000);
    if (get_sr() & 0x30000000) { printf("c1c: SR.BL or SR.RB set, nothing run\n"); return 1; }
    for (int i = 0; i < 16; i++) ((volatile uint32_t *)c1_scratch)[i] = 0x3F800000;
    r4[4] = (uint32_t)c1_scratch;
    c1_r6 = *(volatile uint32_t *)0xFF200000;
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    for (int v = 0; v < 7; v++) {
        c1_r4 = r4[v];
        run_t(v, 0, tli[v], vt[v], 2);
        run_t(v, 1, tlr[v], vt[v], 0);
        run_b(v, 0, tbi[v], vt[v], 2);
        run_b(v, 1, tbr[v], vt[v], 0);
    }
    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    for (int v = 0; v < 7; v++) {
        printf("V%d %-13s loop ill %7.3f (max %7.3f) ref %7.3f (max %7.3f) diff %6.3f\n", v, vn[v], lper[v][0],
               lmax[v][0], lper[v][1], lmax[v][1], lper[v][0] - lper[v][1]);
        for (int k = 0; k < 2; k++) {
            printf("   %s B1-A/B2-B1/B3-B2:", k ? "ref" : "ill");
            for (int r = 0; r < 6; r++) printf(" %ld/%ld/%ld", bst[v][k][r][0], bst[v][k][r][1], bst[v][k][r][2]);
            printf("\n");
        }
    }
    printf("c1c: done aborted %d bara %08lx\n", abt, (unsigned long)*(volatile uint32_t *)0xFF200000);
    return 0;
}
