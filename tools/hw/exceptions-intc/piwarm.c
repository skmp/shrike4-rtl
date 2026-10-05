/* piwarm.c -- excint2's S PI / S PH sweeps with every point repeated back to back (M1a-fix, answering M1a-verify F6;
 * agents/reports/integration/M1a-fix.md).
 *
 * excint2 ran each point of its PI sweep (TCNT1 read to synchronise to Pclk; n adds; an IPRA store making the pending
 * TMU1 underflow level 15 under IMASK 6; 32 adds; k = the add at which the interrupt is taken) twice, with a printf
 * between the two, and got 15/15 21/21 10/15 14/14 17/17 19/19 20/15 20/14 19/17 17/16 14/15 10/14 (run2.log).  The
 * v1 SoC gives 18 16 15 14 17 16 15 14 17 16 15 14.  Question: are n = 0 / 1 / 5 (both repetitions 15, 21, 19) the
 * store's Pclk-phase structure, or instruction-cache effects of a snippet that printf evicted before each repetition?
 * Here every point runs R times with nothing but the window set-up between repetitions (no printf): the first
 * repetition may be cold, the rest are warm.  The same for the PH sweep (LDC SR lowering IMASK 15 -> 6 instead of the
 * store; its warm structure, 7 / 8 by parity, is known [HW]) as a control.  Then the whole PI sweep again in the other
 * loop order (all n, R times), so that a point's result does not depend on the snippet run before it.
 * Output: "PIW n=<n> k: <k0> ... <kR-1>", "PHW n=<n> k: ...", "PIX rep=<r> k(n=0..11): ...".  Safe: the handler of
 * excint_asm.S resumes after 24 records (no storm can hang), every window restores IPRA-C and the TMU. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>

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

typedef struct { uint32_t kind, expevt, intevt, tra, tea, spc, ssr, sgr, sr, pr, r15, r0, r1, r2, pm, tcnt1; } rec_t;
extern rec_t g_recs[32];
extern volatile uint32_t g_nrec, g_irq_mode;
extern uint32_t run_asm(void *fn, uint32_t a0, uint32_t a1, uint32_t a2);
#define X(n) extern char n[];
#define X12(p) X(t_##p##0) X(L_##p##0) X(t_##p##1) X(L_##p##1) X(t_##p##2) X(L_##p##2) X(t_##p##3) X(L_##p##3) \
    X(t_##p##4) X(L_##p##4) X(t_##p##5) X(L_##p##5) X(t_##p##6) X(L_##p##6) X(t_##p##7) X(L_##p##7) X(t_##p##8) \
    X(L_##p##8) X(t_##p##9) X(L_##p##9) X(t_##p##10) X(L_##p##10) X(t_##p##11) X(L_##p##11)
X12(ph) X12(pi)
#define A12(p) {t_##p##0, t_##p##1, t_##p##2, t_##p##3, t_##p##4, t_##p##5, t_##p##6, t_##p##7, t_##p##8, t_##p##9, \
    t_##p##10, t_##p##11}
#define B12(p) {L_##p##0, L_##p##1, L_##p##2, L_##p##3, L_##p##4, L_##p##5, L_##p##6, L_##p##7, L_##p##8, L_##p##9, \
    L_##p##10, L_##p##11}
static char *T_ph[12] = A12(ph), *L_ph[12] = B12(ph), *T_pi[12] = A12(pi), *L_pi[12] = B12(pi);

#define R 8
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
static uint32_t sr15, sr6;
/* one point: -1 when nothing was taken (or TMU1 did not underflow) */
static long pi_one(int n) {
    win_open(0x0000, 0); int ok = tmu1_pend(); run_asm(T_pi[n], 0x0F00, TCNT1, sr6); win_close();
    return (!ok || !g_nrec) ? -1 : (long)(g_recs[0].spc - (uint32_t)L_pi[n]) / 2;
}
static long ph_one(int n) {
    win_open(0x0F00, 0); int ok = tmu1_pend(); run_asm(T_ph[n], sr6, TCNT1, sr15); win_close();
    return (!ok || !g_nrec) ? -1 : (long)(g_recs[0].spc - (uint32_t)L_ph[n]) / 2;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t sr; __asm__ volatile("stc sr,%0" : "=r"(sr));
    sr15 = sr | 0xF0; sr6 = (sr15 & ~0xF0u) | 0x60;
    static long k[12][R];
    printf("piwarm: SR %08lx, %d repetitions per point, no printf between them\n", (unsigned long)sr, R);
    for (int n = 0; n < 12; n++) for (int r = 0; r < R; r++) k[n][r] = pi_one(n);
    for (int n = 0; n < 12; n++) {
        printf("PIW n=%d k:", n);
        for (int r = 0; r < R; r++) printf(" %ld", k[n][r]);
        printf("\n");
    }
    for (int n = 0; n < 12; n++) for (int r = 0; r < R; r++) k[n][r] = ph_one(n);
    for (int n = 0; n < 12; n++) {
        printf("PHW n=%d k:", n);
        for (int r = 0; r < R; r++) printf(" %ld", k[n][r]);
        printf("\n");
    }
    for (int r = 0; r < R; r++) for (int n = 0; n < 12; n++) k[n][r] = pi_one(n);
    for (int r = 0; r < R; r++) {
        printf("PIX rep=%d k(n=0..11):", r);
        for (int n = 0; n < 12; n++) printf(" %ld", k[n][r]);
        printf("\n");
    }
    printf("piwarm: done\n");
    return 0;
}
