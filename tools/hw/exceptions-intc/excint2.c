/* excint2.c -- run 2 of the SH7091 exception / interrupt probes (spec: rtl/v1/spec/exceptions-intc.md).
 * Same window / trampoline / handler as excint.c (excint_asm.S).  Questions left by run 1:
 *   M   MAC.L / MAC.W whose second access (@Rm) faults: is Rn's post-increment kept?
 *   P   a faulting load paired with a younger follower: does the follower's result survive (GPR, T, FCMP T, FADD, FRCHG)?
 *   R   RTE / JMP / LDC SR cost with no interrupt pending (run 1: RTE -> acceptance took ~28 cycles more than LDC SR)
 *   S   acceptance after an IMASK / BL / IPR change as a function of the Pclk phase (a Pclk register read
 *       synchronises the core, then n cycles of adds before the unmasking instruction); a BRA / BF/S slot and an
 *       illegal instruction at the acceptance point
 *   I   IRL levels: KOS routes its ASIC events to Holly level 2 (IRL 13 = 0x3A0); copy them to levels 4 / 6 too
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
#define TSTR 0xFFD80004
#define TCOR1 0xFFD80014
#define TCNT1 0xFFD80018
#define TCR1 0xFFD8001C
#define WTCSR 0xFFC0000C
#define PMCTR0L 0xFF100008

typedef struct { uint32_t kind, expevt, intevt, tra, tea, spc, ssr, sgr, sr, pr, r15, r0, r1, r2, pm, tcnt1; } rec_t;
extern rec_t g_recs[32];
extern volatile uint32_t g_nrec, g_irq_mode, g_ret, g_recovered, g_fpscr_after, g_fr_after[2];
extern uint32_t run_asm(void *fn, uint32_t a0, uint32_t a1, uint32_t a2);
#define X(n) extern char n[];
X(t_mac_rm) X(L_mac_rm) X(t_macw_rm) X(L_macw_rm)
X(t_pk_add) X(L_pk_add) X(t_pk_sett) X(L_pk_sett) X(t_pk_fcmp) X(L_pk_fcmp) X(t_pk_fadd) X(L_pk_fadd)
X(t_pk_frchg) X(L_pk_frchg) X(t_pk_older) X(L_pk_older) X(t_pk_fpe) X(L_pk_fpe) X(t_pk_trapa) X(L_pk_trapa)
X(t_rtet) X(t_jmpt) X(t_ldcsrt) X(t_wait) X(L_wt)
#define X12(p) X(t_##p##0) X(L_##p##0) X(t_##p##1) X(L_##p##1) X(t_##p##2) X(L_##p##2) X(t_##p##3) X(L_##p##3) \
    X(t_##p##4) X(L_##p##4) X(t_##p##5) X(L_##p##5) X(t_##p##6) X(L_##p##6) X(t_##p##7) X(L_##p##7) X(t_##p##8) \
    X(L_##p##8) X(t_##p##9) X(L_##p##9) X(t_##p##10) X(L_##p##10) X(t_##p##11) X(L_##p##11)
X12(ph) X12(pb) X12(ps) X12(pf) X12(pi)
#define A12(p) {t_##p##0, t_##p##1, t_##p##2, t_##p##3, t_##p##4, t_##p##5, t_##p##6, t_##p##7, t_##p##8, t_##p##9, \
    t_##p##10, t_##p##11}
#define B12(p) {L_##p##0, L_##p##1, L_##p##2, L_##p##3, L_##p##4, L_##p##5, L_##p##6, L_##p##7, L_##p##8, L_##p##9, \
    L_##p##10, L_##p##11}
static char *T_ph[12] = A12(ph), *L_ph[12] = B12(ph), *T_pb[12] = A12(pb), *L_pb[12] = B12(pb);
static char *T_ps[12] = A12(ps), *L_ps[12] = B12(ps), *T_pf[12] = A12(pf), *L_pf[12] = B12(pf);
static char *T_pi[12] = A12(pi), *L_pi[12] = B12(pi);

static uint32_t buf[32] __attribute__((aligned(64)));
static uint16_t s_ipra, s_iprb, s_iprc;
static irq_mask_t s_im;
static void tmu1_stop(void) { R8(TSTR) &= ~2; R16(TCR1) = 0; (void)R16(TCR1); }
static void win_open(uint16_t ipra, uint16_t iprb) {
    s_im = irq_disable();
    s_ipra = R16(IPRA); s_iprb = R16(IPRB); s_iprc = R16(IPRC);
    R16(IPRA) = ipra; R16(IPRB) = iprb; (void)R16(IPRC);
    g_nrec = 0; g_irq_mode = 1;
}
static void win_close(void) {
    tmu1_stop(); R16(WTCSR) = 0xA500;
    R16(IPRA) = s_ipra; R16(IPRB) = s_iprb; R16(IPRC) = s_iprc; (void)R16(IPRC);
    irq_restore(s_im);
}
static int tmu1_pend(void) {
    tmu1_stop(); R32(TCOR1) = 0xFFFFFFFF; R32(TCNT1) = 0; R16(TCR1) = 0x20; R8(TSTR) |= 2;
    for (int i = 0; i < 100000; i++) if (R16(TCR1) & 0x100) break;
    R8(TSTR) &= ~2;
    return (R16(TCR1) & 0x100) != 0;
}
static void pr_recs(const char *name, const char *lab) {
    uint32_t n = g_nrec; if (n > 24) n = 24;
    if (n == 0) { printf("%-12s none ret=%08lx fpscr=%08lx fr0=%08lx fr1=%08lx\n", name, (unsigned long)g_ret,
                        (unsigned long)g_fpscr_after, (unsigned long)g_fr_after[0], (unsigned long)g_fr_after[1]); return; }
    for (uint32_t i = 0; i < n; i++) {
        rec_t *r = &g_recs[i];
        printf("%-12s #%lu EXP=%03lx INT=%03lx TEA=%08lx spc=L%+ld ssr=%08lx pr=%08lx r0=%lx r1=%lx r2=%lx fpscr=%08lx fr0=%08lx fr1=%08lx\n",
               name, (unsigned long)i, (unsigned long)r->expevt, (unsigned long)r->intevt, (unsigned long)r->tea,
               lab ? (long)(r->spc - (uint32_t)lab) : 0L, (unsigned long)r->ssr, (unsigned long)r->pr,
               (unsigned long)r->r0, (unsigned long)r->r1, (unsigned long)r->r2, (unsigned long)g_fpscr_after,
               (unsigned long)g_fr_after[0], (unsigned long)g_fr_after[1]);
    }
}
static void one(const char *name, void *fn, const char *lab, uint32_t a0, uint32_t a1, uint32_t a2) {
    win_open(0x0000, 0x0000); run_asm(fn, a0, a1, a2); win_close(); pr_recs(name, lab);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t sr; __asm__ volatile("stc sr,%0" : "=r"(sr));
    uint32_t sr15 = sr | 0xF0, sr6 = (sr15 & ~0xF0u) | 0x60, sr5 = (sr15 & ~0xF0u) | 0x50, sr3 = (sr15 & ~0xF0u) | 0x30;
    uint32_t sr2 = (sr15 & ~0xF0u) | 0x20, sr1 = (sr15 & ~0xF0u) | 0x10;
    uint32_t b = (uint32_t)buf, pm = PMCTR0L;
    for (int i = 0; i < 32; i++) buf[i] = 0x01000000u * (uint32_t)(i + 1);
    printf("excint2: SR %08lx buf %08lx\n", (unsigned long)sr, (unsigned long)b);

    /* ---- M */
    one("mac.l_rm", t_mac_rm, L_mac_rm, b, 0, 0);
    one("mac.w_rm", t_macw_rm, L_macw_rm, b, 0, 0);
    /* ---- P */
    one("pk_add", t_pk_add, L_pk_add, b + 1, 0, 0);
    one("pk_sett", t_pk_sett, L_pk_sett, b + 1, 0, 0);
    one("pk_fcmp", t_pk_fcmp, L_pk_fcmp, b + 1, 0, 0);
    one("pk_fadd", t_pk_fadd, L_pk_fadd, b + 1, 0, 0);
    one("pk_frchg", t_pk_frchg, L_pk_frchg, b + 1, 0, 0);
    one("pk_older", t_pk_older, L_pk_older, b + 1, 0, 0);
    one("pk_fpe", t_pk_fpe, L_pk_fpe, 0, 0x00040080, 0);
    one("pk_trapa", t_pk_trapa, L_pk_trapa, 0, 0, 0);
    /* ---- R */
    {
        static const char *nm[6] = {"rte ssr=sr", "rte imask6", "rte T^1", "jmp", "ldcsr same", "ldcsr imask6"};
        for (int v = 0; v < 6; v++) {
            printf("R %-12s cyc:", nm[v]);
            for (int rep = 0; rep < 5; rep++) {
                win_open(0, 0);
                uint32_t c = v == 0 ? run_asm(t_rtet, sr15, pm, 0) : v == 1 ? run_asm(t_rtet, sr6, pm, 0)
                           : v == 2 ? run_asm(t_rtet, sr15 ^ 1, pm, 0) : v == 3 ? run_asm(t_jmpt, 0, pm, 0)
                           : v == 4 ? run_asm(t_ldcsrt, sr15, pm, 0) : run_asm(t_ldcsrt, sr6, pm, 0);
                win_close();
                printf(" %lu%s", (unsigned long)c, g_nrec ? "*" : "");
            }
            printf("\n");
        }
    }
    /* ---- S: phase sweeps (TMU1 pending at level 15; sync on a TCNT1 read) */
    {
        static const char *nm[2] = {"imask 15->6", "BL 1->0"};
        for (int v = 0; v < 2; v++) {
            uint32_t pre = v ? (sr6 | 0x10000000) : sr15;
            printf("S PH %-11s k(n=0..11):", nm[v]);
            for (int n = 0; n < 12; n++) {
                printf(" ");
                for (int rep = 0; rep < 2; rep++) {
                    win_open(0x0F00, 0); int ok = tmu1_pend(); run_asm(T_ph[n], sr6, TCNT1, pre); win_close();
                    if (!ok || !g_nrec) printf("%s-", rep ? "/" : ""); else
                        printf("%s%ld", rep ? "/" : "", (long)(g_recs[0].spc - (uint32_t)L_ph[n]) / 2);
                }
            }
            printf("\n");
        }
        printf("S PB bra@+6 slot@+8 tgt@+10 spc(n=0..11):");
        for (int n = 0; n < 12; n++) {
            win_open(0x0F00, 0); tmu1_pend(); run_asm(T_pb[n], sr6, TCNT1, sr15); win_close();
            if (!g_nrec) printf(" -"); else printf(" %+ld/%lu", (long)(g_recs[0].spc - (uint32_t)L_pb[n]), (unsigned long)g_recs[0].r0);
        }
        printf("\nS PS bf/s(nt)@+6 slot@+8 spc(n=0..11):");
        for (int n = 0; n < 12; n++) {
            win_open(0x0F00, 0); tmu1_pend(); run_asm(T_ps[n], sr6, TCNT1, sr15); win_close();
            if (!g_nrec) printf(" -"); else printf(" %+ld/%lu", (long)(g_recs[0].spc - (uint32_t)L_ps[n]), (unsigned long)g_recs[0].r0);
        }
        printf("\nS PF ldc sr; fffd: records(n=0..11):");
        for (int n = 0; n < 12; n++) {
            win_open(0x0F00, 0); tmu1_pend(); g_irq_mode = 0; run_asm(T_pf[n], sr6, TCNT1, sr15); win_close();
            printf(" [");
            for (uint32_t i = 0; i < g_nrec && i < 3; i++)
                printf("%s%03lx@%+ld", i ? " " : "", (unsigned long)(g_recs[i].kind == 3 ? g_recs[i].intevt : g_recs[i].expevt),
                       (long)(g_recs[i].spc - (uint32_t)L_pf[n]));
            printf("]");
        }
        printf("\nS PI ipra store k(n=0..11):");
        for (int n = 0; n < 12; n++) {
            printf(" ");
            for (int rep = 0; rep < 2; rep++) {
                win_open(0x0000, 0); tmu1_pend(); run_asm(T_pi[n], 0x0F00, TCNT1, sr6); win_close();
                if (!g_nrec) printf("%s-", rep ? "/" : ""); else
                    printf("%s%ld", rep ? "/" : "", (long)(g_recs[0].spc - (uint32_t)L_pi[n]) / 2);
            }
        }
        printf("\n");
    }
    /* ---- I: IRL levels from Holly */
    {
        printf("I holly: ISTNRM %08lx ISTEXT %08lx ISTERR %08lx IML2 %08lx/%08lx/%08lx IML4 %08lx/%08lx/%08lx IML6 %08lx/%08lx/%08lx\n",
               (unsigned long)R32(0xA05F6900), (unsigned long)R32(0xA05F6904), (unsigned long)R32(0xA05F6908),
               (unsigned long)R32(0xA05F6910), (unsigned long)R32(0xA05F6914), (unsigned long)R32(0xA05F6918),
               (unsigned long)R32(0xA05F6920), (unsigned long)R32(0xA05F6924), (unsigned long)R32(0xA05F6928),
               (unsigned long)R32(0xA05F6930), (unsigned long)R32(0xA05F6934), (unsigned long)R32(0xA05F6938));
        one("irl2_imask1", t_wait, L_wt, sr1, 8000000, 0);
        one("irl2_imask2", t_wait, L_wt, sr2, 8000000, 0);
        for (int lv = 4; lv <= 6; lv += 2) {
            uint32_t base = lv == 4 ? 0xA05F6920 : 0xA05F6930;
            win_open(0, 0);
            uint32_t o0 = R32(base), o1 = R32(base + 4), o2 = R32(base + 8);
            R32(base) = o0 | R32(0xA05F6910); R32(base + 4) = o1 | R32(0xA05F6914); R32(base + 8) = o2 | R32(0xA05F6918);
            (void)R32(base);
            run_asm(t_wait, lv == 4 ? sr3 : sr5, 8000000, 0);
            R32(base) = o0; R32(base + 4) = o1; R32(base + 8) = o2; (void)R32(base);
            win_close();
            pr_recs(lv == 4 ? "irl4_imask3" : "irl6_imask5", L_wt);
        }
    }
    printf("excint2: done\n");
    return 0;
}
