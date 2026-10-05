/* c1j.c -- core-exc probe c1j (WI-CORE-A step A3; rtl/v1/SOC.md 9.1: the P claim before an interrupt).  The loops are
 * in c1j_asm.S: ci2's irqloop (one TMU1 interrupt per iteration, taken at the D leader X right after LDC SR clears BL,
 * the lean handler reads SSR first) with a memory instruction at X or 1 / 2 / 3 places after it, and controls.  Each
 * variant runs with IMASK 14 (the interrupt) and IMASK 15 (none: the reference); E = the difference per iteration.
 * The infrastructure and the safety rules are ci2's (tools/hw/core-integration/ci2.c): interrupts masked in C, KOS's
 * TMU0 / TMU2 masked through IPRA while TMU1 is armed, VBR switched only inside ci_call, every register restored, a
 * budget that aborts a runaway loop. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <dc/perfctr.h>

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R8(a)  (*(volatile uint8_t *)(a))

typedef void (*tfn)(void);
extern void ci_call(tfn fn, void *vbr);
extern uint8_t ci_vbr_t[];

uint32_t ci_ctx[32] __attribute__((aligned(32)));
volatile uint32_t ci_budget, ci_aborted, ci_n;
volatile uint32_t ci_param[8] __attribute__((aligned(32)));
uint32_t ci_recbuf[16 * 16] __attribute__((aligned(32)));
uint32_t *volatile ci_recp;
uint8_t ci_scratch[64] __attribute__((aligned(32)));

#define F(x) extern void x(void);
F(v_none) F(v_ld1) F(v_ldx) F(v_add1) F(v_ld2) F(v_ld3) F(v_st1) F(v_pc1) F(v_pcref) F(v_ldxld1) F(v_pref1)
#undef F

#define IPRA   0xFFD00004
#define TSTR   0xFFD80004
#define TCOR1  0xFFD80014
#define TCNT1  0xFFD80018
#define TCR1   0xFFD8001C

static uint32_t get_sr(void) { uint32_t v; __asm__ volatile("stc sr,%0" : "=r"(v)); return v; }

/* TMU1 underflow interrupt at level 15, pending (the flag stays set until irq_disarm); ci_param[0] = the loop's SR */
static uint16_t ipra_saved;
static int irq_arm(uint32_t imask) {
    ipra_saved = R16(IPRA);
    ci_param[0] = (get_sr() & ~0xF0u) | (imask << 4);
    R16(IPRA) = 0x0F00;
    R8(TSTR) &= ~2; R16(TCR1) = 0x0020; R32(TCOR1) = 0xFFFFFFFF; R32(TCNT1) = 2; R8(TSTR) |= 2;
    for (int i = 0; i < 1000000 && !(R16(TCR1) & 0x100); i++) {}
    R8(TSTR) &= ~2;
    return (R16(TCR1) & 0x100) != 0;
}
static void irq_disarm(void) {
    R8(TSTR) &= ~2;
    (void)R16(TCR1);
    R16(TCR1) = 0;
    R16(IPRA) = ipra_saved;
}

#define N1 16
#define N2 272
/* one loop at one IMASK: (b2 - b1) / (N2 - N1) over 5 repetitions (min per width), as ci2's run_t; the TMU1 request is
 * pending in both (IMASK 15 masks it) */
static double run_t(tfn fn, uint32_t imask, int *ab, uint32_t *left, double *mx) {
    uint64_t b1 = ~0ull, b2 = ~0ull, w2 = 0;
    int ok = 1;
    *ab = 0;
    for (int rep = 0; rep < 5 && ok; rep++)
        for (int w = 0; w < 2 && ok; w++) {
            uint32_t n = w ? N2 : N1;
            ci_n = n; ci_budget = n + 8; ci_recp = ci_recbuf; ci_aborted = 0;
            irq_mask_t im = irq_disable();
            ok = irq_arm(imask);
            uint64_t a = perf_cntr_count(PRFC1);
            if (ok) ci_call(fn, ci_vbr_t);
            uint64_t b = perf_cntr_count(PRFC1);
            irq_disarm();
            irq_restore(im);
            *ab |= ci_aborted;
            uint64_t d = b - a;
            if (w) { if (d < b2) b2 = d; if (d > w2) w2 = d; }
            else if (d < b1) b1 = d;
        }
    *left = ci_budget;
    if (!ok) return -1.0;
    *mx = (double)(int64_t)(w2 - b1) / (N2 - N1);
    return (double)(int64_t)(b2 - b1) / (N2 - N1);
}

static void run_v(const char *name, tfn fn) {
    int ab1, ab2;
    uint32_t l1, l2;
    double m1, m2;
    double irq = run_t(fn, 14, &ab1, &l1, &m1);
    double ref = run_t(fn, 15, &ab2, &l2, &m2);
    if (irq < 0 || ref < 0) { printf("V %-8s TMU1 did not underflow, skipped\n", name); return; }
    printf("V %-8s irq %8.3f (max %8.3f) ref %8.3f (max %8.3f) E %7.3f%s budget_left=%lu/%lu\n", name, irq, m1, ref, m2,
           irq - ref, (ab1 || ab2) ? " ABORTED" : "", (unsigned long)l1, (unsigned long)l2);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("c1j: sr %08lx vbr_t %p scratch %p v_none %p\n", (unsigned long)get_sr(), (void *)ci_vbr_t,
           (void *)ci_scratch, (void *)v_none);
    if (get_sr() & 0x30000000) { printf("c1j: SR.BL or SR.RB set, nothing run\n"); return 1; }
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    run_v("none", v_none);
    run_v("ld1", v_ld1);
    run_v("ldx", v_ldx);
    run_v("add1", v_add1);
    run_v("ld2", v_ld2);
    run_v("ld3", v_ld3);
    run_v("st1", v_st1);
    run_v("pc1", v_pc1);
    run_v("pcref", v_pcref);
    run_v("ldxld1", v_ldxld1);
    run_v("pref1", v_pref1);
    run_v("none", v_none);                 /* again: the first may have been cold */
    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    printf("c1j: done ipra %04x tstr %02x tcr1 %04x\n", R16(IPRA), R8(TSTR), R16(TCR1));
    return 0;
}
