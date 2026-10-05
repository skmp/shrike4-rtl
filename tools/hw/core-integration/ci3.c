/* ci3.c -- third core-integration probe (rtl/v1/spec/core-integration.md section 6): what a slow data access holds.
 * Loops: a P4 register read (EXPEVT) or an uncached RAM read (P2) followed by NOPs, LDC SR, STC SR, a consumer of the
 * loaded value, or a divide whose quotient is used after the load; each has a twin with a cache-hit load.  Also a
 * posted uncached / P4 store followed by NOPs (every store writes back the value the location held; BARA is also
 * saved and restored by C).  No exceptions, no interrupts, MMU off. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <dc/perfctr.h>
#define R32(a) (*(volatile uint32_t *)(a))
typedef void (*tfn)(void);
extern void ci_call(tfn fn, void *vbr);
extern uint8_t ci_vbr_f[], ci_vbr_t[];
uint32_t ci_ctx[32] __attribute__((aligned(32)));
volatile uint32_t ci_budget, ci_aborted, ci_n;
volatile uint32_t ci_param[8] __attribute__((aligned(32)));
volatile uint32_t ci_out[16] __attribute__((aligned(32)));
uint32_t ci_recbuf[16 * 16] __attribute__((aligned(32)));
uint32_t *volatile ci_recp;
uint8_t ci_scratch[64] __attribute__((aligned(32)));
#define F(x) extern void x(void);
F(t3_ram_nop8) F(t3_p4_nop8) F(t3_p2_nop8) F(t3_ram_nop16) F(t3_p2_nop16) F(t3_ram_ldcsr) F(t3_p4_ldcsr)
F(t3_p2_ldcsr) F(t3_ram_stcsr) F(t3_p4_stcsr) F(t3_p2_stcsr) F(t3_ram_use) F(t3_p4_use) F(t3_p2_use)
F(t3_ram_empty) F(t3_p4_empty) F(t3_p2_empty) F(t3_ram_fdiv) F(t3_p4_fdiv) F(t3_p2_fdiv) F(t3_ram_fdiv2)
F(t3_p2_fdiv2) F(t3_ram_st_nop8) F(t3_p2_st_nop8) F(t3_p4_st_nop8)
#undef F
#define N1 16
#define N2 272
static void run_t(const char *name, tfn fn) {
    uint64_t b1 = ~0ull, b2 = ~0ull, w2 = 0;
    int ab = 0;
    for (int rep = 0; rep < 5; rep++)
        for (int w = 0; w < 2; w++) {
            uint32_t n = w ? N2 : N1;
            ci_n = n; ci_budget = 8; ci_recp = ci_recbuf; ci_aborted = 0;
            irq_mask_t im = irq_disable();
            uint64_t a = perf_cntr_count(PRFC1);
            ci_call(fn, ci_vbr_t);
            uint64_t b = perf_cntr_count(PRFC1);
            irq_restore(im);
            ab |= ci_aborted;
            uint64_t d = b - a;
            if (w) { if (d < b2) b2 = d; if (d > w2) w2 = d; }
            else if (d < b1) b1 = d;
        }
    printf("T %-15s %8.3f (max %8.3f)%s\n", name, (double)(int64_t)(b2 - b1) / (N2 - N1),
           (double)(int64_t)(w2 - b1) / (N2 - N1), ab ? " ABORTED" : "");
}
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t sr; __asm__ volatile("stc sr,%0" : "=r"(sr));
    if (sr & 0x30000000) { printf("ci3: SR.BL or SR.RB set, nothing run\n"); return 1; }
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    run_t("ram_empty", t3_ram_empty);   run_t("p4_empty", t3_p4_empty);   run_t("p2_empty", t3_p2_empty);
    run_t("ram_nop8", t3_ram_nop8);     run_t("p4_nop8", t3_p4_nop8);     run_t("p2_nop8", t3_p2_nop8);
    run_t("ram_nop16", t3_ram_nop16);   run_t("p2_nop16", t3_p2_nop16);
    run_t("ram_ldcsr", t3_ram_ldcsr);   run_t("p4_ldcsr", t3_p4_ldcsr);   run_t("p2_ldcsr", t3_p2_ldcsr);
    run_t("ram_stcsr", t3_ram_stcsr);   run_t("p4_stcsr", t3_p4_stcsr);   run_t("p2_stcsr", t3_p2_stcsr);
    run_t("ram_use", t3_ram_use);       run_t("p4_use", t3_p4_use);       run_t("p2_use", t3_p2_use);
    run_t("ram_fdiv", t3_ram_fdiv);     run_t("p4_fdiv", t3_p4_fdiv);     run_t("p2_fdiv", t3_p2_fdiv);
    run_t("ram_fdiv2", t3_ram_fdiv2);   run_t("p2_fdiv2", t3_p2_fdiv2);
    run_t("ram_st_nop8", t3_ram_st_nop8); run_t("p2_st_nop8", t3_p2_st_nop8);
    uint32_t bara = R32(0xFF200000);
    { irq_mask_t im = irq_disable(); run_t("p4_st_nop8", t3_p4_st_nop8); R32(0xFF200000) = bara; irq_restore(im); }
    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    printf("ci3: done bara %08lx\n", (unsigned long)R32(0xFF200000));
    return 0;
}
