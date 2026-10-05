/* c1.c -- soc-design probe C1 (rtl/v1/SOC.md): one program, one handler, four exception classes.
 *
 *   L  loop periods: "X ; dt ; bf" with X = H'FFFD / FADD with FPSCR.EN.I / misaligned MOV.L / TRAPA, against the
 *      reference loop that reaches a byte-identical handler copy through "BRA ; NOP" (its first instruction issues
 *      2 cycles after the BRA).  E(X) = handler first issue - D cycle of X = L(X) - L(ref) + 2.
 *   B  PMCTR brackets: A = PMCTR2L read right before (form d) or K NOPs + BRA before (forms b0..b6) the exception;
 *      B = PMCTR2L read as the handler's first instruction.  Same brackets for the reference BRA path.  Two-pass
 *      forms as in ci2 (lean handler and a replica of ci2's recording handler).
 *   S  PMCTR sample point: PRFC0 counts issue groups (0x13) or cycles (0x23) across fixed instruction sequences.
 *   W  a P4 write (posted?) between two PMCTR reads.
 * Safety: interrupts masked, every handler has an exception budget, every register restored, IPRA / BARA rewritten
 * with their own values only. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <dc/perfctr.h>

typedef void (*tfn)(void);
extern void c1_call(tfn fn, void *vbr, uint32_t skip, uint32_t r5val);
extern uint8_t c1_vt[], c1_vf[], c1_hp[], c1_code_end[];

uint32_t c1_ctx[32] __attribute__((aligned(32)));
volatile uint32_t c1_budget, c1_aborted, c1_n, c1_r5out;
volatile uint32_t c1_ts;
volatile uint32_t c1_out[16] __attribute__((aligned(32)));
uint32_t c1_recbuf[16 * 16] __attribute__((aligned(32)));
uint8_t c1_scratch[64] __attribute__((aligned(32)));

#define F(x) extern void x(void);
F(tl_ill) F(tl_fpe) F(tl_dae) F(tl_trap) F(tl_ref) F(tl_nop)
#define FB(c) F(tb_d_##c) F(tb_b0_##c) F(tb_b1_##c) F(tb_b2_##c) F(tb_b3_##c) F(tb_b4_##c) F(tb_b6_##c)
FB(ill) FB(fpe) FB(dae) FB(trap) FB(ref)
F(tb_p_ill) F(tb_p_trap)
F(ts_ab) F(ts_llb_i) F(ts_llb_d) F(ts_dlb_i) F(ts_nnb) F(ts_eeb)
F(tw_ipra0) F(tw_ipra4) F(tw_ipra8) F(tw_ipra16) F(tw_bara0) F(tw_bara4)
#undef F

static uint32_t get_sr(void) { uint32_t v; __asm__ volatile("stc sr,%0" : "=r"(v)); return v; }

static void call(tfn fn, void *vbr, uint32_t skip, uint32_t r5, uint32_t budget) {
    c1_budget = budget; c1_aborted = 0;
    irq_mask_t im = irq_disable();
    c1_call(fn, vbr, skip, r5);
    irq_restore(im);
}

#define N1 16
#define N2 272
static double run_t(const char *name, tfn fn, uint32_t skip) {
    uint64_t b1 = ~0ull, b2 = ~0ull, w2 = 0;
    int ab = 0;
    for (int rep = 0; rep < 5; rep++)
        for (int w = 0; w < 2; w++) {
            uint32_t n = w ? N2 : N1;
            c1_n = n;
            irq_mask_t im = irq_disable();
            c1_budget = n + 8; c1_aborted = 0;
            uint64_t a = perf_cntr_count(PRFC1);
            c1_call(fn, c1_vt, skip, (uint32_t)&c1_ts);
            uint64_t b = perf_cntr_count(PRFC1);
            irq_restore(im);
            ab |= c1_aborted;
            uint64_t d = b - a;
            if (w) { if (d < b2) b2 = d; if (d > w2) w2 = d; }
            else if (d < b1) b1 = d;
        }
    double per = (double)(int64_t)(b2 - b1) / (N2 - N1);
    printf("L %-6s %8.3f (max %8.3f)%s\n", name, per, (double)(int64_t)(w2 - b1) / (N2 - N1), ab ? " ABORTED" : "");
    return per;
}

#define NREP 6
static void run_b(const char *name, tfn fn, uint32_t skip) {
    printf("B %-9s", name);
    for (int r = 0; r < NREP; r++) {
        c1_ts = 0; c1_out[0] = 0;
        call(fn, c1_vt, skip, (uint32_t)&c1_ts, 4);
        printf(" %4ld", (long)(c1_ts - c1_out[0]));
        if (c1_aborted) printf("A");
    }
    printf("\n");
}

static void run_p(const char *name, tfn fn, uint32_t skip) {
    printf("P %-9s lean:", name);
    for (int r = 0; r < NREP; r++) {
        c1_ts = 0; c1_out[0] = c1_out[1] = 0;
        call(fn, c1_vt, skip, (uint32_t)&c1_ts, 4);
        printf(" %4ld", (long)(c1_ts - c1_out[1]));
        if (c1_aborted) printf("A");
    }
    printf("  ci2-handler (pass1/pass2):");
    for (int r = 0; r < NREP; r++) {
        memset(c1_recbuf, 0, sizeof c1_recbuf);
        c1_out[0] = c1_out[1] = 0;
        call(fn, c1_vf, skip, (uint32_t)c1_recbuf, 4);
        printf(" %ld/%ld", (long)(c1_recbuf[0] - c1_out[0]), (long)(c1_recbuf[16] - c1_out[1]));
        if (c1_aborted) printf("A");
    }
    printf("\n");
}

static void run_df(const char *name, tfn fn, uint32_t skip) {
    printf("Q %-9s ci2-handler direct:", name);
    for (int r = 0; r < NREP; r++) {
        memset(c1_recbuf, 0, sizeof c1_recbuf);
        c1_out[0] = 0;
        call(fn, c1_vf, skip, (uint32_t)c1_recbuf, 4);
        printf(" %4ld", (long)(c1_recbuf[0] - c1_out[0]));
        if (c1_aborted) printf("A");
    }
    printf("\n");
}

static void run_s(const char *name, tfn fn) {
    printf("S %-7s", name);
    for (int r = 0; r < 4; r++) {
        c1_out[2] = 0;
        call(fn, c1_vt, 0, (uint32_t)&c1_ts, 1);
        printf(" %4ld", (long)c1_out[2]);
    }
    printf("\n");
}

static void run_w(const char *name, tfn fn) {
    printf("W %-7s B-A / C-B:", name);
    for (int r = 0; r < 4; r++) {
        c1_out[0] = c1_out[1] = 0;
        call(fn, c1_vt, 0, (uint32_t)&c1_ts, 1);
        printf(" %ld/%ld", (long)c1_out[0], (long)c1_out[1]);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("c1: sr %08lx vt %p hp %p vf %p end %p scratch %p\n", (unsigned long)get_sr(), (void *)c1_vt,
           (void *)c1_hp, (void *)c1_vf, (void *)c1_code_end, (void *)c1_scratch);
    if (get_sr() & 0x30000000) { printf("c1: SR.BL or SR.RB set, nothing run\n"); return 1; }
    /* layout (checked in the listing): the exception-taking tests and both handlers sit below c1_vt + 0x2000; code
       above it (reference / sampling / posting tests) aliases only vector-table gaps or handlers it never uses. */
    for (int i = 0; i < 16; i++) ((volatile uint32_t *)c1_scratch)[i] = 0x3F800000;
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);

    /* L: loop periods */
    double lnop = run_t("nop", tl_nop, 0);
    double lref = run_t("ref", tl_ref, 0);
    double lill = run_t("ill", tl_ill, 2);
    double lfpe = run_t("fpe", tl_fpe, 2);
    double ldae = run_t("dae", tl_dae, 2);
    double ltrp = run_t("trap", tl_trap, 0);
    printf("E (=L-Lref+2): ill %.3f fpe %.3f dae %.3f trap %.3f   (nop loop %.3f)\n", lill - lref + 2,
           lfpe - lref + 2, ldae - lref + 2, ltrp - lref + 2, lnop);
    fflush(stdout);

    /* B: brackets (6 calls each, the first one cold) */
#define RB(c, s) run_b("d_" #c, tb_d_##c, s); run_b("b0_" #c, tb_b0_##c, s); run_b("b1_" #c, tb_b1_##c, s); \
    run_b("b2_" #c, tb_b2_##c, s); run_b("b3_" #c, tb_b3_##c, s); run_b("b4_" #c, tb_b4_##c, s); \
    run_b("b6_" #c, tb_b6_##c, s);
    RB(ref, 0) RB(ill, 2) RB(fpe, 2) RB(dae, 2) RB(trap, 0)
    run_p("p_ill", tb_p_ill, 2);
    run_p("p_trap", tb_p_trap, 0);
    run_df("d_ill", tb_d_ill, 2);
    run_df("d_trap", tb_d_trap, 0);
    fflush(stdout);

    /* W: P4 write between PMCTR reads (PRFC1 = cycles) */
    run_w("ipra0", tw_ipra0); run_w("ipra4", tw_ipra4); run_w("ipra8", tw_ipra8); run_w("ipra16", tw_ipra16);
    run_w("bara0", tw_bara0); run_w("bara4", tw_bara4);

    /* S: PMCTR sample point (PRFC0) */
    static const struct { uint32_t ev; const char *nm; } evs[] = { { 0x13, "issue groups" }, { 0x23, "cycles" },
                                                                   { 0x14, "dual issue" } };
    for (int e = 0; e < 3; e++) {
        perf_cntr_stop(PRFC0);
        perf_cntr_start(PRFC0, (perf_cntr_event_t)evs[e].ev, PMCR_COUNT_CPU_CYCLES);
        printf("S event %02lx (%s):\n", (unsigned long)evs[e].ev, evs[e].nm);
        run_s("ab", ts_ab); run_s("llb_i", ts_llb_i); run_s("llb_d", ts_llb_d); run_s("dlb_i", ts_dlb_i);
        run_s("nnb", ts_nnb); run_s("eeb", ts_eeb);
    }
    perf_cntr_stop(PRFC0);
    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    printf("c1: done budget %lu aborted %lu\n", (unsigned long)c1_budget, (unsigned long)c1_aborted);
    return 0;
}
