/* M4-fix (WI-SOC): M4-verify's program (agents/reports/integration/M4-verify.md), the M4 gate's regression test
 * since M4-fix (tb/mk/v1_soc.mk v1-soc-m4-sub; the verdict: m4/check.py --progs). */
/* irlx.c -- M4 verification: Holly's interrupt levels on the chip's IRL pins against the rest of the INTC (im / rm,
 * the full board, soc_tb --render-delay 3000 --lockstep; no resets).  Holly's source: STARTRENDER raises ISTNRM bits
 * 2, 1, 0 (render done) 3000 Icyc later; IML6 / 4 / 2NRM route them to the pins as levels 6 / 4 / 2 (IRL 1001 / 1011
 * / 1101, board.md 4.1).  Expected values from hm 19.2.2, Tables 19.3-19.5 (the lockstep checker takes INTEVT from
 * the RTL and checks only the level against IMASK, so the order and the IRLM codes are this program's to check):
 *   T1  an IRL level and TMU0 pending together, then IMASK 0: at equal levels IRL is taken first (Table 19.5:
 *       IRL above every on-chip source); a higher TMU0 first; a lower TMU0 second.  Levels 2, 4, 6.
 *   T2  ICR.IRLM = 1 (Table 19.4): the pins are four requests IRL0 13 (0x240), IRL1 10 (0x2A0), IRL2 7 (0x300), IRL3 4
 *       (0x360).  Holly level 2 (1101) is IRL1: 0x2A0, not taken at IMASK 10, taken at 9; level 4 (1011) is IRL2:
 *       0x300 at IMASK 6, not at 7; level 6 (1001) asserts IRL1 and IRL2: IRL1 (0x2A0) wins.
 *   T3  SLEEP, then deep sleep, woken by level 6 (IMASK 5): one take, 0x320
 *   T4  level 6 arriving while a TMU0 handler runs with SR.BL = 1: taken right after its RTE (order 400, 320)
 *   T5  level 6 acknowledged at ISTNRM while IMASK 15: nothing taken when IMASK drops to 0 (the level is gone)
 *   T6  levels 6 and 2 pending, IMASK 3: 6 taken, its handler acknowledges only its own bit; level 2 (not above
 *       IMASK 3) waits; IMASK 1: 0x3A0
 * Output: "IRLX <test> ... got=<codes> want=<codes> OK|FAIL", "IRLX done fails=<n>". */
#include "m4v.h"

static volatile uint32_t g_ack = 7;                        /* the ISTNRM bits the IRL handler acknowledges */
static volatile int g_spin;                                /* the TMU0 handler spins this many Icyc (SR.BL = 1) */
static int fails;

static void h_all(irq_t code, irq_context_t *ctx, void *d) {
  (void)d;
  if (code >= 0x200 && code <= 0x3C0 && !(code & 0x1F)) {
    R32(ISTNRM) = R32(ISTNRM) & g_ack & 7u;
    (void)R32(ISTNRM);
    logent(code, ctx->pc, ctx->sr, pmc(), 0);
    return;
  }
  if (code == 0x400) {
    R8(TSTR) = 0;                                          /* stop first (TCOR0 = ~0: no second underflow) */
    R16(TCR(0)) = (uint16_t)(R16(TCR(0)) & ~UNF);
    (void)R16(TCR(0));
    logent(code, ctx->pc, ctx->sr, pmc(), 0);
    if (g_spin) { const uint32_t t = pmc(); while (pmc() - t < (uint32_t)g_spin) ; }
    return;
  }
  printf("IRLX unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
  R32(SIM_EXIT) = 0x3FE;
  for (;;) {}
}

static void wait_icyc(uint32_t n) { const uint32_t t = pmc(); while (pmc() - t < n) ; }
static void holly_route(uint32_t l2, uint32_t l4, uint32_t l6) {
  R32(IML2NRM) = l2; R32(IML4NRM) = l4; R32(IML6NRM) = l6;
  R32(IML2EXT) = 0; R32(IML4EXT) = 0; R32(IML6EXT) = 0; R32(IML2ERR) = 0; R32(IML4ERR) = 0; R32(IML6ERR) = 0;
  R32(ISTNRM) = 0x3FFFFF; R32(ISTERR) = 0xFFFFFFFF;
  (void)R32(ISTNRM);
}
static void render(void) { R32(STARTRENDER) = 1; (void)R32(ISTNRM); }
static void tmu0(uint32_t lvl, uint32_t counts) {           /* TMU0 underflow after `counts` P/4 counts, level lvl */
  tmu_reset();
  R16(IPRA) = (uint16_t)(lvl << 12);
  R32(TCOR(0)) = 0xFFFFFFFFu; R32(TCNT(0)) = counts; R16(TCR(0)) = UNIE;
  (void)R16(TCR(0));
  R8(TSTR) = 1;
}
static void result(const char *name, const uint32_t *want, int nw) {
  char got[80], wnt[80]; got[0] = wnt[0] = 0;
  int ok = g_nent == nw;
  for (int i = 0; i < g_nent && i < NLOG; i++) {
    sprintf(got + strlen(got), "%s%03lx", i ? "," : "", (unsigned long)g_ent[i].code);
    if (i < nw && g_ent[i].code != want[i]) ok = 0;
  }
  for (int i = 0; i < nw; i++) sprintf(wnt + strlen(wnt), "%s%03lx", i ? "," : "", (unsigned long)want[i]);
  printf("IRLX %s got=%s want=%s %s\n", name, g_nent ? got : "none", nw ? wnt : "none", ok ? "OK" : "FAIL");
  if (!ok) fails++;
  g_nent = 0;
}

int main(void) {
  bm_default = h_all;
  imask(15);
  R8(STBCR2) = 0;
  /* T1: IRL level L and TMU0 at L + d, both pending under IMASK 15, then IMASK 1 */
  static const int L[3] = {2, 4, 6};
  for (int li = 0; li < 3; li++) {
    for (int d = -1; d <= 1; d++) {
      const uint32_t l = (uint32_t)L[li], lt = (uint32_t)(L[li] + d);
      const uint32_t irl = 0x200u + 0x20u * (15u - l);
      holly_route(l == 2 ? 1 : 0, l == 4 ? 2 : 0, l == 6 ? 4 : 0);
      g_ack = 7; g_nent = 0;
      tmu0(lt, 10);
      render();
      wait_icyc(8000);
      imask(0);
      wait_icyc(3000);
      imask(15);
      uint32_t want[2];
      if (lt > l) { want[0] = 0x400; want[1] = irl; } else { want[0] = irl; want[1] = 0x400; }
      char n[40]; sprintf(n, "T1 irl=%lu tmu0=%lu", (unsigned long)l, (unsigned long)lt);
      result(n, want, 2);
      tmu_reset(); R16(IPRA) = 0;
    }
  }
  /* T2: ICR.IRLM = 1 */
  R16(ICR) = 0x0080; (void)R16(ICR);
  {
    static const struct { uint32_t l2, l4, l6, code, im_no, im_yes; const char *n; } t2[3] = {
      {1, 0, 0, 0x2A0, 10, 9, "T2 irlm holly=2 (IRL1)"}, {0, 2, 0, 0x300, 7, 6, "T2 irlm holly=4 (IRL2)"},
      {0, 0, 4, 0x2A0, 10, 9, "T2 irlm holly=6 (IRL1+IRL2)"}};
    for (int i = 0; i < 3; i++) {
      holly_route(t2[i].l2, t2[i].l4, t2[i].l6);
      g_ack = 7; g_nent = 0;
      render();
      wait_icyc(8000);
      imask(t2[i].im_no);
      wait_icyc(3000);
      char n[64]; sprintf(n, "%s imask=%lu", t2[i].n, (unsigned long)t2[i].im_no);
      result(n, 0, 0);
      imask(t2[i].im_yes);
      wait_icyc(3000);
      imask(15);
      sprintf(n, "%s imask=%lu", t2[i].n, (unsigned long)t2[i].im_yes);
      result(n, &t2[i].code, 1);
    }
  }
  R16(ICR) = 0x0000; (void)R16(ICR);
  /* T3: SLEEP / deep sleep woken by level 6 */
  for (int ds = 0; ds < 2; ds++) {
    holly_route(0, 0, 4);
    g_ack = 7; g_nent = 0;
    R8(STBCR2) = ds ? 0x80 : 0; (void)R8(STBCR2);
    render();
    imask(5);
    __asm__ __volatile__("sleep" : : : "memory");
    imask(15);
    R8(STBCR2) = 0;
    static const uint32_t w320 = 0x320;
    result(ds ? "T3 deep-sleep wake" : "T3 sleep wake", &w320, 1);
  }
  /* T4: level 6 arrives while the TMU0 handler runs with SR.BL = 1 */
  {
    holly_route(0, 0, 4);
    g_ack = 7; g_nent = 0; g_spin = 8000;
    imask(4);
    tmu0(5, 10);
    render();
    wait_icyc(12000);
    imask(15);
    g_spin = 0;
    static const uint32_t w[2] = {0x400, 0x320};
    result("T4 irl under bl=1", w, 2);
    tmu_reset(); R16(IPRA) = 0;
  }
  /* T5: acknowledged before IMASK drops */
  {
    holly_route(0, 0, 4);
    g_ack = 7; g_nent = 0;
    render();
    wait_icyc(8000);
    R32(ISTNRM) = 7; (void)R32(ISTNRM);
    wait_icyc(200);
    imask(0);
    wait_icyc(3000);
    imask(15);
    result("T5 acked before unmask", 0, 0);
  }
  /* T6: levels 6 and 2; the first handler acknowledges only its own bit */
  {
    holly_route(1, 0, 4);
    g_ack = 4; g_nent = 0;
    render();
    wait_icyc(8000);
    imask(3);
    wait_icyc(3000);
    static const uint32_t w6 = 0x320, w2 = 0x3A0;
    result("T6 level 6 at imask=3", &w6, 1);
    g_ack = 1;
    imask(1);
    wait_icyc(3000);
    imask(15);
    result("T6 level 2 at imask=1", &w2, 1);
  }
  holly_route(0, 0, 0);
  printf("IRLX done fails=%d\n", fails);
  return 0;
}
