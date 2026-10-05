/* c1b.c -- soc-design probe C1, run 2 (rtl/v1/SOC.md): exception entry cost against the handler's shape.
 * See c1b_asm.S for the five handler shapes H0-H4 and the test forms.  Every loop / bracket result is stored and
 * printed only after all measurements, so no printf runs between repetitions.
 * Safety: interrupts masked, every handler has an exception budget, every register restored. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <dc/perfctr.h>

typedef void (*tfn)(void);
extern void c1_call(tfn fn, void *vbr, uint32_t skip, uint32_t r5val);
extern uint8_t c1_vt0[], c1_vt1[], c1_vt2[], c1_vt3[], c1_vt4[], c1_base[], c1_code_end[];

uint32_t c1_ctx[32] __attribute__((aligned(32)));
volatile uint32_t c1_budget, c1_aborted, c1_n;
volatile uint32_t c1_ts;
volatile uint32_t c1_out[16] __attribute__((aligned(32)));
uint8_t c1_scratch[64] __attribute__((aligned(32)));

#define F(x) extern void x(void);
#define FV(v) F(tl_v##v##_ill) F(tl_v##v##_dae) F(tl_v##v##_ref)
FV(0) FV(1) FV(2) FV(3) FV(4)
F(tl_v2_trap) F(tl_v2_fpe)
#define FB(v) F(tb_v##v##_ill) F(tb_v##v##_dae) F(tb_v##v##_ref)
FB(0) FB(1) FB(3) FB(4)
F(td_v0_0_ill) F(td_v0_2_ill) F(td_v3_0_ill) F(td_v3_2_ill) F(td_v3_0_ref) F(td_v3_2_ref)
F(tj_v3_0_ill) F(tj_v3_2_ill) F(tj_v3_0_trap) F(tj_v3_2_trap)
#undef F

static uint32_t get_sr(void) { uint32_t v; __asm__ volatile("stc sr,%0" : "=r"(v)); return v; }

#define N1 16
#define N2 272
#define MAXR 64
static struct { const char *name; double per, mx; int ab; } lres[MAXR];
static int nl;
static struct { const char *name; long v[6]; int ab; } bres[MAXR];
static int nb;

static void run_t(const char *name, tfn fn, void *vbr, uint32_t skip) {
    uint64_t b1 = ~0ull, b2 = ~0ull, w2 = 0;
    int ab = 0;
    for (int rep = 0; rep < 5; rep++)
        for (int w = 0; w < 2; w++) {
            uint32_t n = w ? N2 : N1;
            c1_n = n;
            irq_mask_t im = irq_disable();
            c1_budget = n + 8; c1_aborted = 0;
            uint64_t a = perf_cntr_count(PRFC1);
            c1_call(fn, vbr, skip, (uint32_t)&c1_ts);
            uint64_t b = perf_cntr_count(PRFC1);
            irq_restore(im);
            ab |= c1_aborted;
            uint64_t d = b - a;
            if (w) { if (d < b2) b2 = d; if (d > w2) w2 = d; }
            else if (d < b1) b1 = d;
        }
    lres[nl].name = name;
    lres[nl].per = (double)(int64_t)(b2 - b1) / (N2 - N1);
    lres[nl].mx = (double)(int64_t)(w2 - b1) / (N2 - N1);
    lres[nl].ab = ab;
    nl++;
}

static void run_b(const char *name, tfn fn, void *vbr, uint32_t skip) {
    int ab = 0;
    for (int r = 0; r < 6; r++) {
        c1_ts = 0; c1_out[0] = 0;
        irq_mask_t im = irq_disable();
        c1_budget = 4; c1_aborted = 0;
        c1_call(fn, vbr, skip, (uint32_t)&c1_ts);
        irq_restore(im);
        ab |= c1_aborted;
        bres[nb].v[r] = (long)(c1_ts - c1_out[0]);
    }
    bres[nb].name = name; bres[nb].ab = ab; nb++;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("c1b: sr %08lx base %p vt0 %p end %p scratch %p\n", (unsigned long)get_sr(), (void *)c1_base,
           (void *)c1_vt0, (void *)c1_code_end, (void *)c1_scratch);
    if (get_sr() & 0x30000000) { printf("c1b: SR.BL or SR.RB set, nothing run\n"); return 1; }
    for (int i = 0; i < 16; i++) ((volatile uint32_t *)c1_scratch)[i] = 0x3F800000;
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);

    /* H0: B first */
    run_t("v0 ill", tl_v0_ill, c1_vt0, 2); run_t("v0 dae", tl_v0_dae, c1_vt0, 2); run_t("v0 ref", tl_v0_ref, c1_vt0, 0);
    run_b("v0 b3 ill", tb_v0_ill, c1_vt0, 2); run_b("v0 b3 dae", tb_v0_dae, c1_vt0, 2);
    run_b("v0 b3 ref", tb_v0_ref, c1_vt0, 0);
    run_b("v0 d0 ill", td_v0_0_ill, c1_vt0, 2); run_b("v0 d2 ill", td_v0_2_ill, c1_vt0, 2);
    /* H1: 4 NOPs, then B */
    run_t("v1 ill", tl_v1_ill, c1_vt1, 2); run_t("v1 dae", tl_v1_dae, c1_vt1, 2); run_t("v1 ref", tl_v1_ref, c1_vt1, 0);
    run_b("v1 b3 ill", tb_v1_ill, c1_vt1, 2); run_b("v1 b3 dae", tb_v1_dae, c1_vt1, 2);
    run_b("v1 b3 ref", tb_v1_ref, c1_vt1, 0);
    /* H2: ci's lean handler (STC SPC first, no time stamp) */
    run_t("v2 ill", tl_v2_ill, c1_vt2, 2); run_t("v2 dae", tl_v2_dae, c1_vt2, 2); run_t("v2 ref", tl_v2_ref, c1_vt2, 0);
    run_t("v2 trap", tl_v2_trap, c1_vt2, 0); run_t("v2 fpe", tl_v2_fpe, c1_vt2, 2);
    /* H3: excint's first line (mov.l @r4,r6) */
    run_t("v3 ill", tl_v3_ill, c1_vt3, 2); run_t("v3 dae", tl_v3_dae, c1_vt3, 2); run_t("v3 ref", tl_v3_ref, c1_vt3, 0);
    run_b("v3 b3 ill", tb_v3_ill, c1_vt3, 2); run_b("v3 b3 dae", tb_v3_dae, c1_vt3, 2);
    run_b("v3 b3 ref", tb_v3_ref, c1_vt3, 0);
    run_b("v3 d0 ill", td_v3_0_ill, c1_vt3, 2); run_b("v3 d2 ill", td_v3_2_ill, c1_vt3, 2);
    run_b("v3 d0 ref", td_v3_0_ref, c1_vt3, 0); run_b("v3 d2 ref", td_v3_2_ref, c1_vt3, 0);
    run_b("v3 j0 ill", tj_v3_0_ill, c1_vt3, 2); run_b("v3 j2 ill", tj_v3_2_ill, c1_vt3, 2);
    run_b("v3 j0 trap", tj_v3_0_trap, c1_vt3, 0); run_b("v3 j2 trap", tj_v3_2_trap, c1_vt3, 0);
    /* H4: STC SPC first, then B */
    run_t("v4 ill", tl_v4_ill, c1_vt4, 2); run_t("v4 dae", tl_v4_dae, c1_vt4, 2); run_t("v4 ref", tl_v4_ref, c1_vt4, 0);
    run_b("v4 b3 ill", tb_v4_ill, c1_vt4, 2); run_b("v4 b3 dae", tb_v4_dae, c1_vt4, 2);
    run_b("v4 b3 ref", tb_v4_ref, c1_vt4, 0);

    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    for (int i = 0; i < nl; i++)
        printf("L %-10s %8.3f (max %8.3f)%s\n", lres[i].name, lres[i].per, lres[i].mx, lres[i].ab ? " ABORTED" : "");
    for (int i = 0; i < nb; i++) {
        printf("B %-11s", bres[i].name);
        for (int r = 0; r < 6; r++) printf(" %4ld", bres[i].v[r]);
        printf("%s\n", bres[i].ab ? " ABORTED" : "");
    }
    printf("c1b: done\n");
    return 0;
}
