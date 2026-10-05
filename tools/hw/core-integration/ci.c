/* ci.c -- core-integration hardware probes for shrike4-rtl/rtl/v1/spec/core-integration.md.
 *
 * Questions (the asm is in ci_asm.S):
 *   F  functional: which PC is saved for a fault in a delay slot (BRA, BT/S not taken, BSR, JSR, RTS) and whether PR is
 *      written; slot-illegal / slot FPU disable / TRAPA in a slot; EXPEVT, TEA, PTEH, TRA, SGR; precise exceptions in
 *      dual-issue pairs (a faulting leader must kill its follower, a faulting follower must not kill its leader); FPU
 *      exception traps: destination, cause and flag fields, an older FDIV in flight; UTLB miss (MMU on, empty TLB);
 *      the handler's entry time stamp against one taken just before the event.
 *   I  interrupts: a TMU1 underflow interrupt (level 15) is pending while IMASK = 15; the test unmasks it with LDC SR,
 *      LDC.L SR, RTE, LDC SR + k NOPs + BRA (k = 0..7), or by clearing BL; which instruction boundary takes it (SPC).
 *      SLEEP: TMU1 running, wake-up by its interrupt, SPC.
 *   T  cycles per iteration of loops that take one exception per iteration (lean handler: stc spc / add / ldc spc /
 *      dt / bt / rte / nop), their no-exception baselines, and loops of 8 accesses to P4 registers / memory.
 * Safety: interrupts are masked (irq_disable) around every test; VBR is switched to the test's vector table only inside
 * ci_call; every handler has an exception budget and sends a runaway test back to C; PTEH, TEA, MMUCR, IPRA and TMU1
 * are restored; the SLEEP test runs only if the pending-interrupt test worked and STBCR / STBCR2 select plain sleep. */
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
F(t_slot_bra) F(t_slot_bts) F(t_slot_bsr) F(t_slot_jsr) F(t_slot_rts) F(t_slot_fpudis) F(t_slot_illegal)
F(t_slot_trapa) F(t_illegal) F(t_trapa) F(t_adr_rd) F(t_adr_wr) F(t_adr_quad) F(t_pair_ld_add) F(t_pair_add_ld)
F(t_pair_ld_fadd) F(t_pair_fadd_add) F(t_pair_add_fadd) F(t_fdiv_trap) F(t_fpu_i) F(t_fpu_z) F(t_utlb_rd)
F(t_utlb_wr) F(t_cal) F(t_irq_ldc) F(t_irq_ldcl) F(t_irq_rte) F(t_irq_br0) F(t_irq_br1) F(t_irq_br2) F(t_irq_br3)
F(t_irq_br4) F(t_irq_br5) F(t_irq_br6) F(t_irq_br7) F(t_irq_bl) F(t_irq_sleep)
F(tt_nop) F(tt_trapa) F(tt_trapa2) F(tt_illegal) F(tt_adr_rd) F(tt_adr_wr) F(tt_ld) F(tt_st) F(tt_fpudis) F(tt_fadd)
F(tt_fpu_i) F(tt_fpu_v) F(tt_fpu_v_ok) F(tt_fdiv_v) F(tt_fdiv_ok) F(tt_fdiv) F(tt_utlb_rd) F(tt_utlb_wr) F(tt_at_p1)
F(tr_ram) F(tr_p2ram) F(tr_expevt) F(tr_ccr) F(tr_expevt7) F(tr_pmctr) F(tr_bara) F(tr_bcr1) F(tr_sar1) F(tr_frqcr)
F(tr_r64cnt) F(tr_ipra) F(tr_tcnt0) F(tr_scfsr2) F(tr_icaa) F(tr_icda) F(tr_itlbaa) F(tr_ocaa) F(tr_ocda) F(tr_utlbaa)
F(tw_ram) F(tw_p2ram) F(tw_pteh) F(tw_bara) F(tw_ipra) F(tw_tcor1) F(tw_sq)
#undef F

#define PTEH   0xFF000000
#define TEA    0xFF00000C
#define MMUCR  0xFF000010
#define STBCR  0xFFC00004
#define STBCR2 0xFFC00010
#define IPRA   0xFFD00004
#define TSTR   0xFFD80004
#define TCOR1  0xFFD80014
#define TCNT1  0xFFD80018
#define TCR1   0xFFD8001C

static uint32_t get_sr(void) { uint32_t v; __asm__ volatile("stc sr,%0" : "=r"(v)); return v; }
static uint32_t get_vbr(void) { uint32_t v; __asm__ volatile("stc vbr,%0" : "=r"(v)); return v; }

static int nrec_last;
static uint32_t irq_seen_420;

static void print_recs(void) {
    int nrec = (int)(ci_recp - ci_recbuf) / 16;
    nrec_last = nrec;
    for (int r = 0; r < nrec && r < 16; r++) {
        uint32_t *q = ci_recbuf + 16 * r;
        printf("    R%d dt=%ld vec=%lx spc=%08lx ssr=%08lx sgr=%08lx pr=%08lx expevt=%03lx intevt=%03lx tea=%08lx "
               "pteh=%08lx tra=%03lx sr=%08lx\n", r, (long)(q[0] - ci_out[15]), (unsigned long)q[1],
               (unsigned long)q[2], (unsigned long)q[3], (unsigned long)q[4], (unsigned long)q[5],
               (unsigned long)q[6], (unsigned long)q[7], (unsigned long)q[8], (unsigned long)q[9],
               (unsigned long)q[10], (unsigned long)q[11]);
        if (q[1] == 6 && (q[7] & 0xfff) == 0x420) irq_seen_420 = 1;
    }
}

static void print_out(const char *kind, const char *name) {
    printf("%s %-15s ab=%lu nrec=%d out:", kind, name, (unsigned long)ci_aborted, (int)(ci_recp - ci_recbuf) / 16);
    for (int k = 0; k < 9; k++) printf(" %08lx", (unsigned long)ci_out[k]);
    printf("\n");
    print_recs();
}

static void run_f(const char *name, tfn fn) {
    memset((void *)ci_out, 0, sizeof ci_out);
    memset(ci_recbuf, 0, sizeof ci_recbuf);
    ci_recp = ci_recbuf; ci_budget = 4; ci_aborted = 0;
    irq_mask_t im = irq_disable();
    ci_call(fn, ci_vbr_f);
    irq_restore(im);
    print_out("F", name);
}

/* TMU1: a pending underflow interrupt (stopped after the underflow), or running with a reload of cnt */
static int tmu1_pending(void) {
    R8(TSTR) &= ~2; R16(TCR1) = 0x0020; R32(TCOR1) = 0xFFFFFFFF; R32(TCNT1) = 2; R8(TSTR) |= 2;
    for (int i = 0; i < 1000000 && !(R16(TCR1) & 0x100); i++) {}
    R8(TSTR) &= ~2;
    return (R16(TCR1) & 0x100) != 0;
}
static void tmu1_running(uint32_t cnt) {
    R8(TSTR) &= ~2; R16(TCR1) = 0x0020; R32(TCOR1) = cnt; R32(TCNT1) = cnt; R8(TSTR) |= 2;
}
static void tmu1_off(void) { R8(TSTR) &= ~2; R16(TCR1) = 0; }

static void run_i(const char *name, tfn fn, int sleep_mode) {
    memset((void *)ci_out, 0, sizeof ci_out);
    memset(ci_recbuf, 0, sizeof ci_recbuf);
    ci_recp = ci_recbuf; ci_budget = 4; ci_aborted = 0;
    irq_mask_t im = irq_disable();
    uint16_t ipra = R16(IPRA);
    ci_param[0] = (get_sr() & ~0xF0u) | 0xE0u;         /* IMASK = 14 */
    R16(IPRA) = 0x0F00;                                /* TMU1 level 15, TMU0 / TMU2 / RTC masked */
    int ok = 1;
    if (sleep_mode) tmu1_running(20000); else ok = tmu1_pending();
    if (ok) ci_call(fn, ci_vbr_f);
    tmu1_off();
    R16(IPRA) = ipra;
    irq_restore(im);
    if (!ok) printf("I %-15s TMU1 did not underflow, skipped\n", name);
    else print_out("I", name);
}

#define N1 16
#define N2 272
static void run_t(const char *name, tfn fn) {
    uint64_t b1 = ~0ull, b2 = ~0ull, w2 = 0;
    int ab = 0;
    for (int rep = 0; rep < 5; rep++)
        for (int w = 0; w < 2; w++) {
            uint32_t n = w ? N2 : N1;
            ci_n = n; ci_budget = n + 8; ci_recp = ci_recbuf; ci_aborted = 0;
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
    uint32_t pteh = R32(PTEH), tea = R32(TEA), mmucr = R32(MMUCR);
    uint8_t stbcr = R8(STBCR), stbcr2 = R8(STBCR2);
    printf("ci: sr %08lx vbr %08lx mmucr %08lx pteh %08lx tea %08lx stbcr %02x stbcr2 %02x ipra %04x tstr %02x tcr1 "
           "%04x vbr_f %p vbr_t %p scratch %p\n", (unsigned long)get_sr(), (unsigned long)get_vbr(),
           (unsigned long)mmucr, (unsigned long)pteh, (unsigned long)tea, stbcr, stbcr2, R16(IPRA), R8(TSTR),
           R16(TCR1), (void *)ci_vbr_f, (void *)ci_vbr_t, (void *)ci_scratch);
    if (get_sr() & 0x30000000) { printf("ci: SR.BL or SR.RB set, nothing run\n"); return 1; }
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);

    run_f("cal", t_cal);
    run_f("slot_bra", t_slot_bra);
    run_f("slot_bts_nt", t_slot_bts);
    run_f("slot_bsr", t_slot_bsr);
    run_f("slot_jsr", t_slot_jsr);
    run_f("slot_rts", t_slot_rts);
    run_f("slot_fpudis", t_slot_fpudis);
    run_f("slot_illegal", t_slot_illegal);
    run_f("slot_trapa", t_slot_trapa);
    run_f("illegal", t_illegal);
    run_f("trapa", t_trapa);
    run_f("adr_rd", t_adr_rd);
    run_f("adr_wr", t_adr_wr);
    run_f("adr_quad", t_adr_quad);
    run_f("pair_ld_add", t_pair_ld_add);
    run_f("pair_add_ld", t_pair_add_ld);
    run_f("pair_ld_fadd", t_pair_ld_fadd);
    run_f("pair_fadd_add", t_pair_fadd_add);
    run_f("pair_add_fadd", t_pair_add_fadd);
    run_f("fdiv_trap", t_fdiv_trap);
    run_f("fpu_i", t_fpu_i);
    run_f("fpu_z", t_fpu_z);
    run_f("utlb_rd", t_utlb_rd);
    run_f("utlb_wr", t_utlb_wr);
    R32(PTEH) = pteh; R32(TEA) = tea;
    printf("ci: sgr expected (C r15 in ci_call) %08lx\n", (unsigned long)ci_ctx[7]);
    fflush(stdout);

    for (int rep = 0; rep < 3; rep++) {
        run_i("irq_ldc", t_irq_ldc, 0);
        run_i("irq_ldcl", t_irq_ldcl, 0);
        run_i("irq_rte", t_irq_rte, 0);
    }
    run_i("irq_br0", t_irq_br0, 0);
    run_i("irq_br1", t_irq_br1, 0);
    run_i("irq_br2", t_irq_br2, 0);
    run_i("irq_br3", t_irq_br3, 0);
    run_i("irq_br4", t_irq_br4, 0);
    run_i("irq_br5", t_irq_br5, 0);
    run_i("irq_br6", t_irq_br6, 0);
    run_i("irq_br7", t_irq_br7, 0);
    run_i("irq_bl", t_irq_bl, 0);
    fflush(stdout);

    run_t("nop", tt_nop);
    run_t("trapa", tt_trapa);
    run_t("trapa+nop", tt_trapa2);
    run_t("illegal", tt_illegal);
    run_t("adr_rd", tt_adr_rd);
    run_t("adr_wr", tt_adr_wr);
    run_t("ld_ok", tt_ld);
    run_t("st_ok", tt_st);
    run_t("fpudis", tt_fpudis);
    run_t("fadd_ok", tt_fadd);
    run_t("fpu_i", tt_fpu_i);
    run_t("fpu_v", tt_fpu_v);
    run_t("fpu_v_ok", tt_fpu_v_ok);
    run_t("fdiv+fpu_v", tt_fdiv_v);
    run_t("fdiv+fadd_ok", tt_fdiv_ok);
    run_t("fdiv", tt_fdiv);
    run_t("utlb_rd", tt_utlb_rd);
    run_t("utlb_wr", tt_utlb_wr);
    run_t("at1_p1_ld", tt_at_p1);
    R32(PTEH) = pteh; R32(TEA) = tea;
    run_t("rd_ram x8", tr_ram);
    run_t("rd_p2ram x8", tr_p2ram);
    run_t("rd_expevt x8", tr_expevt);
    run_t("rd_ccr x8", tr_ccr);
    run_t("rd_expevt_a7 x8", tr_expevt7);
    run_t("rd_pmctr x8", tr_pmctr);
    run_t("rd_bara x8", tr_bara);
    run_t("rd_bcr1 x8", tr_bcr1);
    run_t("rd_sar1 x8", tr_sar1);
    run_t("rd_frqcr x8", tr_frqcr);
    run_t("rd_r64cnt x8", tr_r64cnt);
    run_t("rd_ipra x8", tr_ipra);
    run_t("rd_tcnt0 x8", tr_tcnt0);
    run_t("rd_scfsr2 x8", tr_scfsr2);
    run_t("rd_icaa x8", tr_icaa);
    run_t("rd_icda x8", tr_icda);
    run_t("rd_itlbaa x8", tr_itlbaa);
    run_t("rd_ocaa x8", tr_ocaa);
    run_t("rd_ocda x8", tr_ocda);
    run_t("rd_utlbaa x8", tr_utlbaa);
    run_t("wr_ram x8", tw_ram);
    run_t("wr_p2ram x8", tw_p2ram);
    run_t("wr_pteh x8", tw_pteh);
    run_t("wr_bara x8", tw_bara);
    run_t("wr_ipra x8", tw_ipra);
    run_t("wr_tcor1 x8", tw_tcor1);
    run_t("wr_sq x8", tw_sq);
    fflush(stdout);

    /* SLEEP last, only if a pending TMU1 interrupt was taken above and SLEEP means plain sleep */
    if (irq_seen_420 && !(stbcr & 0x80) && !(stbcr & 0x04) && !(stbcr2 & 0x80)) {
        for (int rep = 0; rep < 3; rep++) run_i("irq_sleep", t_irq_sleep, 1);
    } else {
        printf("ci: SLEEP test skipped (irq_seen %lu stbcr %02x stbcr2 %02x)\n", (unsigned long)irq_seen_420, stbcr,
               stbcr2);
    }

    R32(PTEH) = pteh; R32(TEA) = tea;
    perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();
    printf("ci: done mmucr %08lx ipra %04x tstr %02x tcr1 %04x vbr %08lx\n", (unsigned long)R32(MMUCR), R16(IPRA),
           R8(TSTR), R16(TCR1), (unsigned long)get_vbr());
    return 0;
}
