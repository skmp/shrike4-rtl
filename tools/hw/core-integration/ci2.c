/* ci2.c -- second core-integration probe (follow-ups to ci.c; see rtl/v1/spec/core-integration.md section 6).
 *
 *   F  warm (second pass) exception / interrupt entry time stamps: TRAPA, illegal, LDC SR (IMASK), BL clear, RTE,
 *      LDC SR + k NOPs + BRA (k = 0..9): where the interrupt is accepted once the code is in the instruction cache;
 *      MAC.L with misaligned operands (which access is first, are Rm / Rn updated); a faulting load paired with
 *      FRCHG / FSCHG (is the mode flip cancelled); an older inexact FDIV in flight across a V trap (FPSCR.cause).
 *   T  one TMU1 interrupt per loop iteration (BL toggled, IMASK 14 throughout) against the same loop with IMASK 15;
 *      V trap on a signalling NaN that reaches the FADD through the load bypass vs. from the register file;
 *      a misaligned load as leader / follower of an ADD pair.
 * Same safety rules and infrastructure as ci.c (ci2_asm.S is ci_asm.S's infrastructure plus these tests). */
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
extern uint8_t ci_vbr_f[], ci_vbr_t[];

uint32_t ci_ctx[32] __attribute__((aligned(32)));
volatile uint32_t ci_budget, ci_aborted, ci_n;
volatile uint32_t ci_param[8] __attribute__((aligned(32)));
volatile uint32_t ci_out[16] __attribute__((aligned(32)));
uint32_t ci_recbuf[16 * 16] __attribute__((aligned(32)));
uint32_t *volatile ci_recp;
uint8_t ci_scratch[64] __attribute__((aligned(32)));

#define F(x) extern void x(void);
F(t2_trapa) F(t2_illegal) F(t2_irq_ldc) F(t2_irq_bl) F(t2_irq_rte) F(t2_irq_br0) F(t2_irq_br1) F(t2_irq_br2)
F(t2_irq_br3) F(t2_irq_br4) F(t2_irq_br5) F(t2_irq_br6) F(t2_irq_br7) F(t2_irq_br8) F(t2_irq_br9) F(t2_mac_mis)
F(t2_frchg_kill) F(t2_fdiv_cause)
F(tt2_irqloop) F(tt2_ldcloop) F(tt2_vbyp) F(tt2_vbyp_ok) F(tt2_vfar) F(tt2_vfar_ok) F(tt2_pair_l1) F(tt2_pair_l0)
F(tt2_pair_ok)
#undef F

#define PTEH   0xFF000000
#define TEA    0xFF00000C
#define IPRA   0xFFD00004
#define TSTR   0xFFD80004
#define TCOR1  0xFFD80014
#define TCNT1  0xFFD80018
#define TCR1   0xFFD8001C

static uint32_t get_sr(void) { uint32_t v; __asm__ volatile("stc sr,%0" : "=r"(v)); return v; }

static void print_out(const char *kind, const char *name) {
    int nrec = (int)(ci_recp - ci_recbuf) / 16;
    printf("%s %-15s ab=%lu nrec=%d out:", kind, name, (unsigned long)ci_aborted, nrec);
    for (int k = 0; k < 9; k++) printf(" %08lx", (unsigned long)ci_out[k]);
    printf("\n");
    for (int r = 0; r < nrec && r < 16; r++) {
        uint32_t *q = ci_recbuf + 16 * r;
        uint32_t t0 = ci_out[14 + (r & 1)];
        printf("    R%d dt=%ld vec=%lx spc=%08lx ssr=%08lx expevt=%03lx intevt=%03lx tea=%08lx pr=%08lx sr=%08lx\n", r,
               (long)(q[0] - t0), (unsigned long)q[1], (unsigned long)q[2], (unsigned long)q[3],
               (unsigned long)q[6], (unsigned long)q[7], (unsigned long)q[8], (unsigned long)q[5],
               (unsigned long)q[11]);
    }
}

/* TMU1 underflow interrupt at level 15, pending (the flag stays set: nothing here clears it until tmu1_off) */
static uint16_t ipra_saved;
static int irq_arm(void) {
    ipra_saved = R16(IPRA);
    ci_param[0] = (get_sr() & ~0xF0u) | 0xE0u;         /* IMASK = 14 */
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

static void run_f(const char *name, tfn fn, int irq) {
    memset((void *)ci_out, 0, sizeof ci_out);
    memset(ci_recbuf, 0, sizeof ci_recbuf);
    ci_recp = ci_recbuf; ci_budget = 6; ci_aborted = 0;
    irq_mask_t im = irq_disable();
    int ok = irq ? irq_arm() : 1;
    if (ok) ci_call(fn, ci_vbr_f);
    if (irq) irq_disarm();
    irq_restore(im);
    if (!ok) printf("F %-15s TMU1 did not underflow, skipped\n", name);
    else print_out("F", name);
}

#define N1 16
#define N2 272
static void run_t(const char *name, tfn fn, int irq) {
    uint64_t b1 = ~0ull, b2 = ~0ull, w2 = 0;
    int ab = 0, ok = 1;
    for (int rep = 0; rep < 5 && ok; rep++)
        for (int w = 0; w < 2 && ok; w++) {
            uint32_t n = w ? N2 : N1;
            ci_n = n; ci_budget = n + 8; ci_recp = ci_recbuf; ci_aborted = 0;
            irq_mask_t im = irq_disable();
            if (irq) ok = irq_arm();
            uint64_t a = perf_cntr_count(PRFC1);
            if (ok) ci_call(fn, ci_vbr_t);
            uint64_t b = perf_cntr_count(PRFC1);
            if (irq) irq_disarm();
            irq_restore(im);
            ab |= ci_aborted;
            uint64_t d = b - a;
            if (w) { if (d < b2) b2 = d; if (d > w2) w2 = d; }
            else if (d < b1) b1 = d;
        }
    if (!ok) { printf("T %-15s TMU1 did not underflow, skipped\n", name); return; }
    printf("T %-15s %8.3f (max %8.3f)%s budget_left=%lu\n", name, (double)(int64_t)(b2 - b1) / (N2 - N1),
           (double)(int64_t)(w2 - b1) / (N2 - N1), ab ? " ABORTED" : "", (unsigned long)ci_budget);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t pteh = R32(PTEH), tea = R32(TEA);
    printf("ci2: sr %08lx vbr_f %p vbr_t %p scratch %p\n", (unsigned long)get_sr(), (void *)ci_vbr_f,
           (void *)ci_vbr_t, (void *)ci_scratch);
    if (get_sr() & 0x30000000) { printf("ci2: SR.BL or SR.RB set, nothing run\n"); return 1; }
    ((volatile uint32_t *)ci_scratch)[0] = 0x7FC00000;   /* SH-4 signalling NaN */
    ((volatile uint32_t *)ci_scratch)[1] = 0x3F800000;   /* 1.0 */
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);

    run_f("trapa x2", t2_trapa, 0);
    run_f("illegal x2", t2_illegal, 0);
    run_f("irq_ldc x2", t2_irq_ldc, 1);
    run_f("irq_bl x2", t2_irq_bl, 1);
    run_f("irq_rte x2", t2_irq_rte, 1);
    run_f("irq_br0 x2", t2_irq_br0, 1);
    run_f("irq_br1 x2", t2_irq_br1, 1);
    run_f("irq_br2 x2", t2_irq_br2, 1);
    run_f("irq_br3 x2", t2_irq_br3, 1);
    run_f("irq_br4 x2", t2_irq_br4, 1);
    run_f("irq_br5 x2", t2_irq_br5, 1);
    run_f("irq_br6 x2", t2_irq_br6, 1);
    run_f("irq_br7 x2", t2_irq_br7, 1);
    run_f("irq_br8 x2", t2_irq_br8, 1);
    run_f("irq_br9 x2", t2_irq_br9, 1);
    run_f("irq_ldc x2 b", t2_irq_ldc, 1);
    run_f("irq_rte x2 b", t2_irq_rte, 1);
    run_f("mac_mis", t2_mac_mis, 0);
    run_f("frchg_kill", t2_frchg_kill, 0);
    run_f("fdiv_cause", t2_fdiv_cause, 0);
    R32(PTEH) = pteh; R32(TEA) = tea;
    fflush(stdout);

    run_t("irqloop", tt2_irqloop, 1);
    run_t("ldcloop", tt2_ldcloop, 0);
    run_t("vbyp", tt2_vbyp, 0);
    run_t("vbyp_ok", tt2_vbyp_ok, 0);
    run_t("vfar", tt2_vfar, 0);
    run_t("vfar_ok", tt2_vfar_ok, 0);
    run_t("pair_l1", tt2_pair_l1, 0);
    run_t("pair_l0", tt2_pair_l0, 0);
    run_t("pair_ok", tt2_pair_ok, 0);

    R32(PTEH) = pteh; R32(TEA) = tea;
    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    printf("ci2: done ipra %04x tstr %02x tcr1 %04x\n", R16(IPRA), R8(TSTR), R16(TCR1));
    return 0;
}
