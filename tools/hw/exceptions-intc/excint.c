/* excint.c -- SH7091 exception / interrupt / INTC facts measured on the console (spec: rtl/v1/spec/exceptions-intc.md).
 *
 * Every test runs with interrupts masked (irq_disable), with VBR pointed at a private vector table (excint_asm.S) for
 * the duration of one asm snippet only.  The handler records EXPEVT / INTEVT / TRA / TEA / SPC / SSR / SGR / SR / PR /
 * R15 / bank-0 R0-R2 / PMCTR0 (CPU cycles) / TCNT1 and resumes in the trampoline.  Only TMU1 (free under KOS) and the
 * WDT in interval mode are used as interrupt sources; KOS's TMU0 / TMU2 are masked through IPRA during a window, the
 * ASIC IRLs by IMASK >= 6.  Every register changed is restored before interrupts are unmasked again.
 * Output: one line per handler record:  <test> #i k=<vector 1/2/3> EXP INT TRA TEA spc=<label>+off ssr sr sgr=r15?
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R8(a)  (*(volatile uint8_t *)(a))
#define IPRA 0xFFD00004
#define IPRB 0xFFD00008
#define IPRC 0xFFD0000C
#define ICR  0xFFD00000
#define TSTR 0xFFD80004
#define TCOR1 0xFFD80014
#define TCNT1 0xFFD80018
#define TCR1 0xFFD8001C
#define WTCNT 0xFFC00008
#define WTCSR 0xFFC0000C
#define MMUCR 0xFF000010
#define QACR0 0xFF000038
#define PMCTR0L 0xFF100008

typedef struct { uint32_t kind, expevt, intevt, tra, tea, spc, ssr, sgr, sr, pr, r15, r0, r1, r2, pm, tcnt1; } rec_t;
extern rec_t g_recs[32];
extern volatile uint32_t g_nrec, g_irq_mode, g_ret, g_recovered, g_fpscr_after;
extern uint32_t run_asm(void *fn, uint32_t a0, uint32_t a1, uint32_t a2);

#define X(n) extern char n[];
X(t_regmask)
X(t_trapa) X(L_trapa) X(t_ill) X(L_ill) X(t_zero) X(L_zero) X(t_slot_ill) X(L_slot_ill) X(t_slot_trapa) X(L_slot_trapa)
X(t_slot_bra) X(L_slot_bra) X(t_slot_mova) X(L_slot_mova) X(t_slot_movpc) X(L_slot_movpc) X(t_slot_ldcsr) X(L_slot_ldcsr)
X(t_slot_bt) X(L_slot_bt) X(t_slot_zero) X(L_slot_zero)
X(t_fd_fadd) X(L_fd_fadd) X(t_fd_lds) X(L_fd_lds) X(t_fd_stsfpul) X(L_fd_stsfpul) X(t_fd_fschg) X(L_fd_fschg)
X(t_fd_fmov) X(L_fd_fmov) X(t_fd_fffd) X(L_fd_fffd) X(t_fd_slot) X(L_fd_slot) X(t_fd_slot_fffd) X(L_fd_slot_fffd)
X(t_fd_ldsl) X(L_fd_ldsl)
X(t_fpe_fadd) X(L_fpe_fadd) X(t_fpe_slot) X(L_fpe_slot) X(t_fpe_sqrtneg) X(L_fpe_sqrtneg) X(t_fpe_sqrtpos)
X(L_fpe_sqrtpos) X(t_fpe_div0) X(L_fpe_div0) X(t_fpe_div1) X(L_fpe_div1)
X(t_dae_wrd) X(L_dae_wrd) X(t_dae_lwr) X(L_dae_lwr) X(t_dae_lpost) X(L_dae_lpost) X(t_dae_lpre) X(L_dae_lpre)
X(t_dae_fmovd_rd) X(L_dae_fmovd_rd) X(t_dae_fmovd_wr) X(L_dae_fmovd_wr) X(t_dae_fmovs) X(L_dae_fmovs)
X(t_dae_movca) X(L_dae_movca) X(t_dae_ocbwb) X(L_dae_ocbwb) X(t_dae_ocbp) X(L_dae_ocbp) X(t_dae_ocbi) X(L_dae_ocbi)
X(t_dae_pref) X(L_dae_pref) X(t_dae_tas) X(L_dae_tas) X(t_dae_mac2) X(L_dae_mac2) X(t_dae_mac1) X(L_dae_mac1)
X(t_dae_macw2) X(L_dae_macw2) X(t_dae_ldclsr) X(L_dae_ldclsr)
X(t_ds_bra) X(L_ds_bra) X(t_ds_bsr) X(L_ds_bsr) X(t_ds_jsr) X(L_ds_jsr) X(t_ds_bfs_nt) X(L_ds_bfs_nt)
X(t_ds_bts_t) X(L_ds_bts_t) X(t_ds_rts) X(L_ds_rts) X(t_ds_jmp_iae) X(L_ds_jmp_iae) X(oddtgt) X(t_ds_rts_iae)
X(L_ds_rts_iae) X(oddtgt2)
X(t_rte_rb) X(t_rte_t)
X(t_user) X(u_rd) X(u_wr) X(u_pref) X(u_ocbi) X(u_stcsr) X(u_gbr) X(u_stcsgr) X(u_ldtlb) X(u_sleep) X(u_rte) X(L_u_rte)
X(u_slot) X(u_nop)
X(t_chain_ldcsr) X(L_cl1) X(t_chain_ldclsr) X(L_cll1) X(t_chain_rte) X(L_cr1) X(t_chain_bl) X(L_cb1)
X(t_chain_st16) X(L_cs1) X(t_chain_st8) X(L_c81) X(t_twoirq) X(L_tw1) X(t_prio_ill) X(L_pi)
X(t_brap0) X(L_brap0) X(t_brap1) X(L_brap1) X(t_brap2) X(L_brap2) X(t_brap3) X(L_brap3)
X(t_brap4) X(L_brap4) X(t_brap5) X(L_brap5) X(t_brap6) X(L_brap6) X(t_brap7) X(L_brap7)
X(t_sleep) X(L_sl) X(L_sl2) X(t_busy) X(L_bz) X(t_wait) X(L_wt)
X(t_cost_base) X(t_cost_trapa) X(t_cost_ill) X(t_cost_dae) X(t_cost_fd) X(t_cost_fpe)

static uint32_t buf[32] __attribute__((aligned(64)));
static uint32_t sqbuf[16] __attribute__((aligned(64)));
static uint32_t sr_c;           /* SR of the C code with IMASK 15 */

/* ---- window: interrupts masked, KOS TMU0 / TMU2 masked through IPRA, TMU1 / WDT quiet at the end */
static uint16_t s_ipra, s_iprb, s_iprc;
static irq_mask_t s_im;
static void tmu1_stop(void) { R8(TSTR) &= ~2; R16(TCR1) = 0; (void)R16(TCR1); }
static void wdt_stop(void) { R16(WTCSR) = 0xA500; (void)R8(WTCSR); }
static void win_open(uint16_t ipra, uint16_t iprb) {
    s_im = irq_disable();
    s_ipra = R16(IPRA); s_iprb = R16(IPRB); s_iprc = R16(IPRC);
    R16(IPRA) = ipra; R16(IPRB) = iprb; (void)R16(IPRC);
    g_nrec = 0; g_irq_mode = 1;
}
static void win_close(void) {
    tmu1_stop(); wdt_stop();
    R16(IPRA) = s_ipra; R16(IPRB) = s_iprb; R16(IPRC) = s_iprc; (void)R16(IPRC);
    irq_restore(s_im);
}
/* TMU1: counter value, UNIE; pend = run it until UNF is set (then stop) */
static void tmu1_arm(uint32_t cnt, int unie) {
    tmu1_stop(); R32(TCOR1) = 0xFFFFFFFF; R32(TCNT1) = cnt; R16(TCR1) = unie ? 0x20 : 0x00;
}
static int tmu1_pend(int unie) {
    tmu1_arm(0, unie); R8(TSTR) |= 2;
    for (int i = 0; i < 100000; i++) if (R16(TCR1) & 0x100) break;
    R8(TSTR) &= ~2;
    return (R16(TCR1) & 0x100) != 0;
}
/* WDT interval mode: overflow once, stop with IOVF still set */
static int wdt_pend(void) {
    if (R8(WTCSR) & 0x40) return -1;            /* watchdog mode active: do not touch */
    R16(WTCSR) = 0xA500; R16(WTCNT) = 0x5AFF; R16(WTCSR) = 0xA580;
    for (int i = 0; i < 100000; i++) if (R8(WTCSR) & 0x08) break;
    R16(WTCSR) = 0xA508;                        /* TME = 0, IOVF written 1 (no effect) */
    return (R8(WTCSR) & 0x08) != 0;
}

static void pr_recs(const char *name, const char *lab) {
    uint32_t n = g_nrec; if (n > 24) n = 24;
    if (n == 0) { printf("%-14s none  ret=%08lx rec=%lu fpscr=%08lx\n", name, (unsigned long)g_ret,
                        (unsigned long)g_recovered, (unsigned long)g_fpscr_after); return; }
    for (uint32_t i = 0; i < n; i++) {
        rec_t *r = &g_recs[i];
        printf("%-14s #%lu k%lu EXP=%03lx INT=%03lx TRA=%03lx TEA=%08lx spc=%08lx(L%+ld) ssr=%08lx sr=%08lx %s pr=%08lx "
               "r0=%lx r1=%lx r2=%lx fpscr=%08lx\n",
               name, (unsigned long)i, (unsigned long)r->kind, (unsigned long)r->expevt, (unsigned long)r->intevt,
               (unsigned long)r->tra, (unsigned long)r->tea, (unsigned long)r->spc,
               lab ? (long)(r->spc - (uint32_t)lab) : 0L, (unsigned long)r->ssr, (unsigned long)r->sr,
               r->sgr == r->r15 ? "sgr=r15" : "SGR!=R15", (unsigned long)r->pr, (unsigned long)r->r0,
               (unsigned long)r->r1, (unsigned long)r->r2, (unsigned long)g_fpscr_after);
    }
}
static void one(const char *name, void *fn, const char *lab, uint32_t a0, uint32_t a1, uint32_t a2) {
    win_open(0x0000, 0x0000);
    run_asm(fn, a0, a1, a2);
    win_close();
    pr_recs(name, lab);
}
static uint32_t u0(const void *p) { return (uint32_t)p & 0x1FFFFFFF; }

/* one user-mode run: RTE into `code` (its U0 alias unless p1) with SR = usr, r1 = arg */
static void user(const char *name, void *code, int p1, uint32_t usr, uint32_t arg) {
    uint32_t pc = p1 ? (uint32_t)code : u0(code);
    win_open(0x0000, 0x0000);
    run_asm(t_user, pc, usr, arg);
    win_close();
    pr_recs(name, (const char *)pc);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t sr, vbr;
    __asm__ volatile("stc sr,%0" : "=r"(sr));
    __asm__ volatile("stc vbr,%0" : "=r"(vbr));
    sr_c = sr | 0xF0;
    printf("excint: SR %08lx VBR %08lx ICR %04x IPRA %04x IPRB %04x IPRC %04x IPRD(FFD00010) %04x STBCR %02x STBCR2 %02x\n",
           (unsigned long)sr, (unsigned long)vbr, R16(ICR), R16(IPRA), R16(IPRB), R16(IPRC), R16(0xFFD00010),
           R8(0xFFC00004), R8(0xFFC00010));
    printf("excint: EXPEVT %08lx INTEVT %08lx TRA %08lx TEA %08lx MMUCR %08lx QACR0 %08lx WTCSR %02x WTCNT %02x\n",
           (unsigned long)R32(0xFF000024), (unsigned long)R32(0xFF000028), (unsigned long)R32(0xFF000020),
           (unsigned long)R32(0xFF00000C), (unsigned long)R32(MMUCR), (unsigned long)R32(QACR0), R8(WTCSR), R8(WTCNT));

    /* ---- A. writable bits */
    {
        uint32_t m[9] = {0};
        irq_mask_t im = irq_disable();
        uint32_t e = R32(0xFF000024), i = R32(0xFF000028), t = R32(0xFF000020);
        R32(0xFF000024) = 0xFFFFFFFF; uint32_t em = R32(0xFF000024); R32(0xFF000024) = e;
        R32(0xFF000028) = 0xFFFFFFFF; uint32_t imk = R32(0xFF000028); R32(0xFF000028) = i;
        R32(0xFF000020) = 0xFFFFFFFF; uint32_t tm = R32(0xFF000020); R32(0xFF000020) = t;
        uint16_t icr = R16(ICR), a = R16(IPRA), b = R16(IPRB), c = R16(IPRC);
        R16(ICR) = 0xFFFF; uint16_t icrm = R16(ICR); R16(ICR) = icr; (void)R16(ICR);
        R16(IPRA) = 0xFFFF; uint16_t am = R16(IPRA); R16(IPRA) = a;
        R16(IPRB) = 0xFFFF; uint16_t bm = R16(IPRB); R16(IPRB) = b;
        R16(IPRC) = 0xFFFF; uint16_t cm = R16(IPRC); R16(IPRC) = c; (void)R16(IPRC);
        irq_restore(im);
        win_open(0, 0); run_asm(t_regmask, (uint32_t)m, 0, 0); win_close();
        printf("A masks: EXPEVT %08lx INTEVT %08lx TRA %08lx ICR %04x IPRA %04x IPRB %04x IPRC %04x\n",
               (unsigned long)em, (unsigned long)imk, (unsigned long)tm, icrm, am, bm, cm);
        printf("A masks: SR %08lx VBR %08lx SSR %08lx SPC %08lx DBR %08lx GBR %08lx FPSCR %08lx (SGR %08lx R15 %08lx)\n",
               (unsigned long)m[0], (unsigned long)m[1], (unsigned long)m[2], (unsigned long)m[3], (unsigned long)m[4],
               (unsigned long)m[5], (unsigned long)m[6], (unsigned long)m[7], (unsigned long)m[8]);
    }

    /* ---- B. general exceptions: codes, SPC, SSR, SGR, entry SR */
    one("trapa", t_trapa, L_trapa, 0, 0, 0);
    one("ill_fffd", t_ill, L_ill, 0, 0, 0);
    one("ill_0000", t_zero, L_zero, 0, 0, 0);
    one("slot_fffd", t_slot_ill, L_slot_ill, 0, 0, 0);
    one("slot_trapa", t_slot_trapa, L_slot_trapa, 0, 0, 0);
    one("slot_bra", t_slot_bra, L_slot_bra, 0, 0, 0);
    one("slot_mova", t_slot_mova, L_slot_mova, 0, 0, 0);
    one("slot_movpc", t_slot_movpc, L_slot_movpc, 0, 0, 0);
    one("slot_ldcsr", t_slot_ldcsr, L_slot_ldcsr, 0, 0, 0);
    one("slot_bt", t_slot_bt, L_slot_bt, 0, 0, 0);
    one("slot_0000", t_slot_zero, L_slot_zero, 0, 0, 0);
    one("fd_fadd", t_fd_fadd, L_fd_fadd, 0, 0, 0);
    one("fd_lds_fpscr", t_fd_lds, L_fd_lds, 0, 0, 0);
    one("fd_sts_fpul", t_fd_stsfpul, L_fd_stsfpul, 0, 0, 0);
    one("fd_fschg", t_fd_fschg, L_fd_fschg, 0, 0, 0);
    one("fd_fmov", t_fd_fmov, L_fd_fmov, 0, 0, 0);
    one("fd_fffd", t_fd_fffd, L_fd_fffd, 0, 0, 0);
    one("fd_slot", t_fd_slot, L_fd_slot, 0, 0, 0);
    one("fd_slot_fffd", t_fd_slot_fffd, L_fd_slot_fffd, 0, 0, 0);
    one("fd_ldsl_fpul", t_fd_ldsl, L_fd_ldsl, 0, 0, 0);
    one("fpe_fadd_I", t_fpe_fadd, L_fpe_fadd, 0x00040080, 0, 0);
    one("fpe_slot_I", t_fpe_slot, L_fpe_slot, 0x00040080, 0, 0);
    one("fpe_sqrt-1_V", t_fpe_sqrtneg, L_fpe_sqrtneg, 0x00040800, 0, 0);
    one("fpe_sqrt+1_V", t_fpe_sqrtpos, L_fpe_sqrtpos, 0x00040800, 0, 0);
    one("fpe_div0_Z", t_fpe_div0, L_fpe_div0, 0x00040400, 0, 0);
    one("fpe_div1_Z", t_fpe_div1, L_fpe_div1, 0x00040400, 0, 0);
    one("fpe_div0_none", t_fpe_div0, L_fpe_div0, 0x00040000, 0, 0);

    /* ---- data address errors: buf is 64-byte aligned; its words hold a sane SR (for the LDC.L SR probe) */
    for (int i = 0; i < 32; i++) buf[i] = sr_c;
    uint32_t b = (uint32_t)buf;
    printf("buf=%08lx\n", (unsigned long)b);
    one("dae_w_rd+1", t_dae_wrd, L_dae_wrd, b, 0, 0);
    one("dae_l_wr+2", t_dae_lwr, L_dae_lwr, b, 0, 0);
    one("dae_l_@r+", t_dae_lpost, L_dae_lpost, b, 0, 0);
    one("dae_l_@-r", t_dae_lpre, L_dae_lpre, b, 0, 0);
    one("dae_fmovd_rd+4", t_dae_fmovd_rd, L_dae_fmovd_rd, b, 0, 0);
    one("dae_fmovd_wr+4", t_dae_fmovd_wr, L_dae_fmovd_wr, b, 0, 0);
    one("dae_fmovs+2", t_dae_fmovs, L_dae_fmovs, b, 0, 0);
    one("dae_movca+2", t_dae_movca, L_dae_movca, b, 0, 0);
    one("dae_ocbwb+1", t_dae_ocbwb, L_dae_ocbwb, b, 0, 0);
    one("dae_ocbp+1", t_dae_ocbp, L_dae_ocbp, b, 0, 0);
    one("dae_ocbi+1", t_dae_ocbi, L_dae_ocbi, (uint32_t)sqbuf, 0, 0);
    one("dae_pref+1", t_dae_pref, L_dae_pref, b, 0, 0);
    one("dae_tas+1", t_dae_tas, L_dae_tas, b, 0, 0);
    one("dae_macl_2nd", t_dae_mac2, L_dae_mac2, b, 0, 0);
    one("dae_macl_both", t_dae_mac1, L_dae_mac1, b, 0, 0);
    one("dae_macw_2nd", t_dae_macw2, L_dae_macw2, b, 0, 0);
    for (int i = 0; i < 32; i++) buf[i] = sr_c;        /* movca / tas / stores above may have changed the line */
    one("dae_ldcl_sr", t_dae_ldclsr, L_dae_ldclsr, b, 0, 0);

    /* ---- delay-slot faults */
    one("ds_bra", t_ds_bra, L_ds_bra, b, 0, 0);
    one("ds_bsr", t_ds_bsr, L_ds_bsr, b, 0, 0);
    one("ds_jsr", t_ds_jsr, L_ds_jsr, b, 0, 0);
    one("ds_bfs_nt", t_ds_bfs_nt, L_ds_bfs_nt, b, 0, 0);
    one("ds_bts_t", t_ds_bts_t, L_ds_bts_t, b, 0, 0);
    one("ds_rts", t_ds_rts, L_ds_rts, b, 0, 0);
    one("iae_jmp_odd", t_ds_jmp_iae, oddtgt, 0, 0, 0);
    one("iae_rts_odd", t_ds_rts_iae, oddtgt2, 0, 0, 0);
    one("rte_slot_rb", t_rte_rb, NULL, 0, 0, 0);
    one("rte_slot_t", t_rte_t, NULL, 0, 0, 0);

    /* ---- user mode (MD = 0, IMASK 15 unless noted); user code runs from the U0 alias of the P1 text */
    {
        uint32_t us = 0x000000F0, mm = R32(MMUCR), q0 = R32(QACR0);
        uint32_t sqa = 0xE0000000 | (u0(sqbuf) & 0x03FFFFC0);    /* SQ0 (bit 5 = 0) */
        user("u_rd_p1", u_rd, 0, us, 0x8C000000);
        user("u_rd_p4", u_rd, 0, us, 0xFF000020);
        user("u_rd_p2", u_rd, 0, us, 0xA0000000);
        user("u_wr_sq_md0", u_wr, 0, us, sqa);
        irq_mask_t im = irq_disable(); R32(QACR0) = (u0(sqbuf) >> 26 & 7) << 2; R32(MMUCR) = mm | 0x200;
        (void)R32(MMUCR); irq_restore(im);
        user("u_wr_sq_md1", u_wr, 0, us, sqa);
        user("u_pref_sq_md1", u_pref, 0, us, sqa);
        im = irq_disable(); R32(MMUCR) = mm; (void)R32(MMUCR); R32(QACR0) = q0; irq_restore(im);
        user("u_pref_p1", u_pref, 0, us, (uint32_t)sqbuf);
        user("u_ocbi_p1", u_ocbi, 0, us, (uint32_t)sqbuf);
        user("u_stc_sr", u_stcsr, 0, us, 0);
        user("u_gbr", u_gbr, 0, us, 0);
        user("u_stc_sgr", u_stcsgr, 0, us, 0);
        user("u_ldtlb", u_ldtlb, 0, us, 0);
        user("u_rte", u_rte, 0, us, 0);
        user("u_slot_stcsr", u_slot, 0, us, 0);
        user("u_fetch_p1", u_nop, 1, us, 0);
        user("u_fetch_u0", u_nop, 0, us, 0);
        /* SLEEP in user mode: TMU1 armed as a safety wake-up (level 15, user IMASK 6) */
        win_open(0x0F00, 0x0000); tmu1_arm(5000, 1); R8(TSTR) |= 2;
        run_asm(t_user, u0(u_sleep), 0x00000060, 0);
        win_close(); pr_recs("u_sleep", u_sleep);
    }

    /* ---- C. interrupts.  IMASK 6 is "open" (TMU1 / WDT at levels > 6; the ASIC IRLs 6/4/2 stay masked) */
    uint32_t sr6 = (sr_c & ~0xF0u) | 0x60, sr15 = sr_c;
    uint32_t pm = PMCTR0L;
    static uint32_t srword[1];
    printf("C: sr6=%08lx sr15=%08lx\n", (unsigned long)sr6, (unsigned long)sr15);
    /* C1 acceptance after unmasking a pending TMU1 interrupt: k = adds executed after the unmasking instruction */
    for (int v = 0; v < 6; v++) {
        static const char *nm[6] = {"ldc_sr", "ldc.l_sr", "rte", "bl_1to0", "ipr_write", "unie_write"};
        char *lab = v == 0 ? L_cl1 : v == 1 ? L_cll1 : v == 2 ? L_cr1 : v == 3 ? L_cb1 : L_cs1;
        printf("C1 %-10s k:", nm[v]);
        int dt[8];
        for (int rep = 0; rep < 8; rep++) {
            win_open(v == 4 ? 0x0000 : 0x0F00, 0);
            int ok = tmu1_pend(v != 5);
            switch (v) {
            case 0: run_asm(t_chain_ldcsr, sr6, pm, 0); break;
            case 1: srword[0] = sr6; run_asm(t_chain_ldclsr, (uint32_t)srword, pm, 0); break;
            case 2: run_asm(t_chain_rte, sr6, pm, 0); break;
            case 3: run_asm(t_chain_bl, sr6, sr6 | 0x10000000, pm); break;
            case 4: run_asm(t_chain_st16, IPRA, 0x0F00, sr6); break;
            case 5: run_asm(t_chain_st16, TCR1, 0x0120, sr6); break;
            }
            win_close();
            rec_t *r = &g_recs[0];
            if (!ok || g_nrec == 0) { printf(" [none ok=%d]", ok); dt[rep] = -1; continue; }
            printf(" %ld/%lu", (long)(r->spc - (uint32_t)lab) / 2, (unsigned long)r->r0);
            dt[rep] = (int)(r->pm - r->r1);
        }
        if (v <= 3) { printf("  cyc(pmctr before unmask -> handler):"); for (int rep = 0; rep < 8; rep++) printf(" %d", dt[rep]); }
        printf("  INT=%03lx ssr=%08lx\n", (unsigned long)g_recs[0].intevt, (unsigned long)g_recs[0].ssr);
    }
    /* C1g: TMU1 started by a TSTR store with TCNT1 = 0 / 1 / 2: adds executed before the interrupt */
    for (int cnt = 0; cnt < 3; cnt++) {
        printf("C1g tstr cnt=%d k:", cnt);
        for (int rep = 0; rep < 12; rep++) {
            win_open(0x0F00, 0);
            tmu1_arm((uint32_t)cnt, 1);
            run_asm(t_chain_st8, TSTR, R8(TSTR) | 2, sr6);
            win_close();
            rec_t *r = &g_recs[0];
            if (g_nrec == 0) { printf(" none"); continue; }
            printf(" %ld/%lu", (long)(r->spc - (uint32_t)L_c81) / 2, (unsigned long)(r->pm - r->r1));
        }
        printf("\n");
    }
    /* C1h: BRA + slot around the acceptance point */
    {
        char *tb[8] = {t_brap0, t_brap1, t_brap2, t_brap3, t_brap4, t_brap5, t_brap6, t_brap7};
        char *lb[8] = {L_brap0, L_brap1, L_brap2, L_brap3, L_brap4, L_brap5, L_brap6, L_brap7};
        for (int p = 0; p < 8; p++) {
            printf("C1h bra@%d spc-bra:", p);
            for (int rep = 0; rep < 6; rep++) {
                win_open(0x0F00, 0); tmu1_pend(1); run_asm(tb[p], sr6, 0, 0); win_close();
                if (g_nrec == 0) printf(" none"); else printf(" %+ld/%lu", (long)(g_recs[0].spc - (uint32_t)lb[p]),
                                                             (unsigned long)g_recs[0].r0);
            }
            printf("\n");
        }
    }
    /* C1i: pending interrupt unmasked by LDC SR, next instruction illegal: which is taken first */
    win_open(0x0F00, 0); tmu1_pend(1); g_irq_mode = 0; run_asm(t_prio_ill, sr6, 0, 0); win_close();
    pr_recs("prio_irq_ill", L_pi);
    /* C2: two sources pending, same / different levels (irq_mode 0: handler clears the source and returns) */
    {
        static const struct { uint16_t ipra, iprb; uint32_t sr; const char *nm; } cs[5] = {
            {0x0A00, 0xA000, 0, "same_lvl10"}, {0x0A00, 0xC000, 0, "wdt12_tmu10"}, {0x0C00, 0xA000, 0, "tmu12_wdt10"},
            {0x0600, 0x0000, 0, "tmu6_imask6"}, {0x0700, 0x0000, 0, "tmu7_imask6"}};
        for (int c = 0; c < 5; c++) {
            win_open(cs[c].ipra, cs[c].iprb);
            int t1 = tmu1_pend(1), w = c < 3 ? wdt_pend() : 0;
            g_irq_mode = 0;
            run_asm(t_twoirq, sr6, 0, 0);
            win_close();
            printf("C2 %-12s tmu=%d wdt=%d:", cs[c].nm, t1, w);
            for (uint32_t i = 0; i < g_nrec && i < 4; i++)
                printf(" INT=%03lx@%ld", (unsigned long)g_recs[i].intevt, (long)(g_recs[i].spc - (uint32_t)L_tw1) / 2);
            printf("%s\n", g_nrec ? "" : " none");
        }
    }
    /* C3: IRL from the ASIC (vblank, KOS routes it to IRQ level 6 = IRL 9): IMASK 5 accepts it, IMASK 6 does not */
    {
        uint32_t sr5 = (sr_c & ~0xF0u) | 0x50;
        one("irl_imask5", t_wait, L_wt, sr5, 6000000, 0);
        one("irl_imask6", t_wait, L_wt, sr6, 6000000, 0);
    }
    /* C4: SLEEP wake-up by TMU1 (TCNT1 = 64 counts of Pphi/4) vs a busy loop */
    for (int rep = 0; rep < 4; rep++) {
        for (int s = 0; s < 2; s++) {
            win_open(0x0F00, 0); tmu1_arm(64, 1);
            run_asm(s ? t_busy : t_sleep, TSTR, R8(TSTR) | 2, sr6);
            win_close();
            rec_t *r = &g_recs[0];
            if (g_nrec == 0) { printf("C4 %s none\n", s ? "busy" : "sleep"); continue; }
            printf("C4 %-5s INT=%03lx spc=L%+ld cyc(start->handler)=%lu tcnt1=%08lx ssr=%08lx\n", s ? "busy" : "sleep",
                   (unsigned long)r->intevt, (long)(r->spc - (uint32_t)(s ? L_bz : L_sl)), (unsigned long)(r->pm - r->r1),
                   (unsigned long)r->tcnt1, (unsigned long)r->ssr);
        }
    }

    /* ---- D. exception entry cost: PMCTR read right before the faulting instruction -> first handler instruction */
    {
        uint32_t odd = (uint32_t)buf + 1;
        printf("D cost (cyc):");
        for (int rep = 0; rep < 4; rep++) {
            win_open(0, 0); uint32_t base = run_asm(t_cost_base, pm, 0, 0); win_close();
            win_open(0, 0); run_asm(t_cost_trapa, pm, 0, 0); win_close(); uint32_t tr = g_recs[0].pm - g_recs[0].r1;
            win_open(0, 0); run_asm(t_cost_ill, pm, 0, 0); win_close(); uint32_t il = g_recs[0].pm - g_recs[0].r1;
            win_open(0, 0); run_asm(t_cost_dae, pm, odd, 0); win_close(); uint32_t da = g_recs[0].pm - g_recs[0].r1;
            win_open(0, 0); run_asm(t_cost_fd, pm, 0, 0); win_close(); uint32_t fd = g_recs[0].pm - g_recs[0].r1;
            win_open(0, 0); run_asm(t_cost_fpe, pm, 0x00040080, 0); win_close(); uint32_t fp = g_recs[0].pm - g_recs[0].r1;
            printf(" [base %lu trapa %lu ill %lu dae %lu fd %lu fpe %lu]", (unsigned long)base, (unsigned long)tr,
                   (unsigned long)il, (unsigned long)da, (unsigned long)fd, (unsigned long)fp);
        }
        printf("\n");
    }
    printf("excint: done\n");
    return 0;
}
