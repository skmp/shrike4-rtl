/* sleepwk.c -- M1a adversarial test: SLEEP against the Pclk landing, the CPG's wait (v1_p4bus.pclk_wpend ->
 * v1_cpg.p4_wpend, SOC.md 2.5), the INTC's wake, BL = 1 sleep (hm 9.5.4: "interrupts are accepted even if the BL bit
 * in the SR register is 1"), deep sleep (STBCR2.DSLP), and an IMASK-lowering race into SLEEP.
 * Every case must wake (no hang), enter the handler once with the right INTEVT, SPC = SLEEP + 2 when the SLEEP ran
 * (or the SLEEP itself when the interrupt came first), and the lockstep checker must accept every SLEEP / WAKE / IRQ.
 * Output: "SLP <case> n=<entries> intevt=<code> at=<SPC - SLEEP> ssr=<SSR> counted=<PMCTR delta>" */
#include "adv.h"

extern uint32_t sl_st8(uint32_t, uint32_t, uint32_t), sl_st16(uint32_t, uint32_t, uint32_t);
extern char sl_st8_slp[], sl_st16_slp[];
typedef uint32_t (*sk_t)(uint32_t);
extern sk_t sk_tab[13];
extern uint32_t sk_slp_tab[13];

static void out(const char *name, int k, uint32_t slp, uint32_t cnt) {
  if (k >= 0) printf("SLP %s k=%d", name, k); else printf("SLP %s", name);
  if (g_nlog)
    printf(" n=%lu intevt=%03lx at=%ld ssr=%08lx counted=%lu\n", (unsigned long)g_nlog, (unsigned long)g_log[0].intevt,
           (long)(int32_t)(g_log[0].spc - slp), (unsigned long)g_log[0].ssr, (unsigned long)cnt);
  else
    printf(" n=0 counted=%lu\n", (unsigned long)cnt);
}

/* TMU0 counting down from n (Pphi / 4: 16 Icyc a count) with UNIE, not started */
static void tmu0_arm(uint32_t n, uint16_t tcr) {
  tmu_reset();
  R32(TCOR0) = 0xFFFFFFFFu;
  R32(TCNT0) = n;
  R16(TCR0) = tcr;
  (void)R16(TCR0);
}
static void tmu0_pending(uint16_t tcr) {
  tmu0_arm(2, tcr);
  R8(TSTR) = 1;
  while (!(R16(TCR0) & UNF)) ;
  R8(TSTR) = 0;
  (void)R8(TSTR);
}

int main(void) {
  printf("sleepwk: start\n");
  g_clr_unie = 1; g_noreadback = 0; g_resume = 0;
  uint32_t c;

  /* (a) TSTR <- 1 (posted), SLEEP: TMU0 underflows 40 counts later and wakes the CPU */
  R16(IPRA) = 0x5000; (void)R16(IPRA);
  tmu0_arm(40, UNIE); clrlog();
  c = sl_st8(TSTR, 1, 0x40000000u);
  out("tstr", -1, (uint32_t)sl_st8_slp, c);

  /* (b) UNF pending, UNIE 0; TCR0 <- UNF | UNIE (posted; writing UNF = 1 keeps it), SLEEP: the source rises after the
   * write lands (strobe + 8) + 4 / 5 */
  tmu0_pending(0); clrlog();
  c = sl_st16(TCR0, UNF | UNIE, 0x40000000u);
  out("unie", -1, (uint32_t)sl_st16_slp, c);

  /* (c) UNF and UNIE, IPRA TMU0 = 0; IPRA <- 0x5000 (posted), SLEEP */
  R16(IPRA) = 0; (void)R16(IPRA);
  tmu0_pending(UNIE); clrlog();
  c = sl_st16(IPRA, 0x5000, 0x40000000u);
  out("ipra", -1, (uint32_t)sl_st16_slp, c);
  R16(IPRA) = 0x5000; (void)R16(IPRA);

  /* (d) deep sleep: STBCR2.DSLP <- 1 (posted), SLEEP; TSTR started just before */
  tmu0_arm(40, UNIE);
  R8(TSTR) = 1; (void)R8(TSTR);
  clrlog();
  c = sl_st8(STBCR2, 0x80, 0x40000000u);
  out("dslp", -1, (uint32_t)sl_st8_slp, c);
  R8(STBCR2) = 0; (void)R8(STBCR2);

  /* (e) BL = 1, IMASK 0: TSTR <- 1, SLEEP; the interrupt is accepted in sleep (hm 9.5.4), SSR.BL = 1 */
  tmu0_arm(40, UNIE); clrlog();
  c = sl_st8(TSTR, 1, 0x50000000u);
  set_sr(0x400000F0u);
  out("bl1", -1, (uint32_t)sl_st8_slp, c);

  /* (f) BL = 1, IMASK 0, the source already pending: SLEEP right behind the write that enables it (TCR0.UNIE) */
  tmu0_pending(0); clrlog();
  c = sl_st16(TCR0, UNF | UNIE, 0x50000000u);
  set_sr(0x400000F0u);
  out("bl1unie", -1, (uint32_t)sl_st16_slp, c);

  /* (g) IMASK 15 -> 0 by LDC SR with TMU0 pending, k NOPs, SLEEP: taken before the SLEEP, or the SLEEP sleeps and
   * the request wakes it */
  /* (a take at the SLEEP returns to the SLEEP, which then needs a second wake: TMU0 keeps counting from 300 with
   * UNIE kept, so its next underflow is the backstop; every entry is printed) */
  g_clr_unie = 0;
  for (int k = 0; k < 13; k++) {
    set_sr(0x400000F0u);
    tmu0_pending(UNIE);
    R32(TCNT0) = 300; R8(TSTR) = 1; (void)R8(TSTR);
    clrlog();
    c = sk_tab[k](0x40000000u);
    set_sr(0x400000F0u);
    R8(TSTR) = 0; (void)R8(TSTR);
    out("imask", k, sk_slp_tab[k], c);
    for (uint32_t i = 1; i < g_nlog; i++)
      printf("SLP imask k=%d entry=%lu intevt=%03lx at=%ld\n", k, (unsigned long)i, (unsigned long)g_log[i].intevt,
             (long)(int32_t)(g_log[i].spc - sk_slp_tab[k]));
  }
  g_clr_unie = 1;

  /* (h) the same race with a posted STBCR2 <- 0 write (no-op) right before: the CPG waits for it to land */
  tmu_reset(); R16(IPRA) = 0;
  printf("sleepwk: done\n");
  return 0;
}
