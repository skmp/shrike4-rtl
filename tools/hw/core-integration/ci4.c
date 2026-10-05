/* ci4.c -- fourth core-integration probe (rtl/v1/SOC.md 9.3, WI-CORE-F): what the MA hold `mah` holds in the FPU.
 * Loops (ci4_asm.S; the same bodies as tb/v1_fpuhold_probe.h, which prints the RTL's prediction for each): an FDIV,
 * FDIV DR, FIPR, FTRV, FSCA, MUL.L or FADD DR before a slow load (uncached P2, or a cacheable load miss) and a consumer
 * of its result after it; FDIV at several distances before / after the load; an independent FE / EX op right after
 * the load.  Each has a cache-hit twin.  Cycles per iteration as ci3: (min of 5 x N2 - min of 5 x N1) / 256 from
 * PMCTR elapsed mode.  No exceptions, no interrupts, MMU off; ci_call saves and restores every register it touches
 * (FPSCR included: the double loops set PR). */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <dc/perfctr.h>
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
uint8_t ci4_big[0x8000] __attribute__((aligned(32)));     /* two lines 16 KB apart share an operand-cache set */
#define K(p) extern void t4_##p##_fd(void); extern void t4_##p##_fd_n2(void); extern void t4_##p##_fd_n4(void); \
    extern void t4_##p##_fd_n6(void); extern void t4_##p##_fd_n8(void); extern void t4_##p##_fd_u2(void); \
    extern void t4_##p##_fdd(void); extern void t4_##p##_fipr(void); extern void t4_##p##_ftrv(void); \
    extern void t4_##p##_fsca(void); extern void t4_##p##_mul(void); extern void t4_##p##_dbl(void); \
    extern void t4_##p##_efe(void); extern void t4_##p##_eex(void);
K(ram) K(p2)
#undef K
extern void t4_ram_fdm(void), t4_miss_fdm(void), t4_ram_fdm_n4(void), t4_miss_fdm_n4(void), t4_ram_em(void), t4_miss_em(void);
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
#define R(p) \
    run_t(#p "_fd", t4_##p##_fd);       run_t(#p "_fd_n2", t4_##p##_fd_n2); run_t(#p "_fd_n4", t4_##p##_fd_n4); \
    run_t(#p "_fd_n6", t4_##p##_fd_n6); run_t(#p "_fd_n8", t4_##p##_fd_n8); run_t(#p "_fd_u2", t4_##p##_fd_u2); \
    run_t(#p "_fdd", t4_##p##_fdd);     run_t(#p "_fipr", t4_##p##_fipr);   run_t(#p "_ftrv", t4_##p##_ftrv);   \
    run_t(#p "_fsca", t4_##p##_fsca);   run_t(#p "_mul", t4_##p##_mul);     run_t(#p "_dbl", t4_##p##_dbl);     \
    run_t(#p "_efe", t4_##p##_efe);     run_t(#p "_eex", t4_##p##_eex);
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t sr; __asm__ volatile("stc sr,%0" : "=r"(sr));
    if (sr & 0x30000000) { printf("ci4: SR.BL or SR.RB set, nothing run\n"); return 1; }
    uint32_t fpscr; __asm__ volatile("sts fpscr,%0" : "=r"(fpscr));
    printf("ci4: FPSCR %08lx\n", (unsigned long)fpscr);
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    R(ram)
    R(p2)
    run_t("ram_fdm", t4_ram_fdm);     run_t("miss_fdm", t4_miss_fdm);
    run_t("ram_fdm_n4", t4_ram_fdm_n4); run_t("miss_fdm_n4", t4_miss_fdm_n4);
    run_t("ram_em", t4_ram_em);       run_t("miss_em", t4_miss_em);
    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    printf("ci4: done\n");
    return 0;
}
