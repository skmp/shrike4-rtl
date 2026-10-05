/* M4-fix (WI-SOC): M4-verify's run-time (agents/reports/integration/M4-verify.md), the M4 gate's regression test
 * since M4-fix (tb/mk/v1_soc.mk v1-soc-m4-sub; the verdict: m4/check.py --progs). */
/* m4v.c -- the M4 verification's run-time helpers (m4v.h): the early hook m4vcrt0.S calls before crt0's CCR write,
 * and the WDT helpers.  m4v_early runs before .bss is cleared and before VBR is set (the stub's VBR 0x8C000000 is
 * still live: an exception here is soc_tb's "boot failure"): it uses only its stack and fixed P2 addresses. */
#include "m4v.h"

void m4v_early(void) {
  volatile cap_t *c = CAP;
  const uint32_t t = R32(PMCTR1L);
  /* the WDT first: a WDT reset keeps TME (SOC.md 2.3), so it counts on and would fire again */
  const uint32_t ws = R8(WTCSR), wc = R8(WTCNT);
  R16(WTCSR) = (uint16_t)(0xA500u | (ws & 7u));             /* TME 0, WT/IT 0, RSTS 0, WOVF / IOVF cleared */
  (void)R8(WTCSR);
  if (c->magic != M4V_MAGIC) { c->magic = M4V_MAGIC; c->boots = 1; } else c->boots = c->boots + 1;
  c->early_pmc = t;
  c->wtcsr = ws; c->wtcnt = wc;
  c->expevt = R32(EXPEVT); c->intevt = R32(INTEVT); c->tra = R32(TRA); c->tea = R32(TEA);
  c->pteh = R32(PTEH); c->mmucr = R32(MMUCR); c->ccr = R32(CCR); c->qacr0 = R32(QACR0); c->qacr1 = R32(QACR1);
  c->frqcr = R16(FRQCR); c->stbcr = R8(STBCR); c->stbcr2 = R8(STBCR2);
  c->pmcr1 = R16(PMCR1); c->pmcr2 = R16(PMCR2);
  c->pmctr1h = R32(PMCTR1H); c->pmctr1l = R32(PMCTR1L); c->pmctr2h = R32(PMCTR2H); c->pmctr2l = R32(PMCTR2L);
  c->rfcr = R16(RFCR); c->pctrb = R32(PCTRB); c->pdtrb = R16(PDTRB);
  { uint32_t sr; __asm__ __volatile__("stc sr,%0" : "=r"(sr)); c->sr = sr; }
  /* the watch lists the previous boot left (the arrays as the reset left them: CCR = 0, nothing fills) */
  uint32_t n = WATCH->n_ocw; if (n > 8) n = 8;
  c->n_ocw = n;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t e = WATCH->ocw_idx[i] & 511u;
    c->ocw_idx[i] = e; c->oca[i] = R32(OC_ADDR(e));
    for (uint32_t w = 0; w < 8; w++) c->ocd[i][w] = R32(OC_DATA(e, w));
  }
  n = WATCH->n_icw; if (n > 8) n = 8;
  c->n_icw = n;
  for (uint32_t i = 0; i < n; i++) { const uint32_t e = WATCH->icw_idx[i] & 255u; c->icw_idx[i] = e; c->ica[i] = R32(IC_ADDR(e)); }
  for (uint32_t i = 0; i < 64; i++) { c->utlb_a[i] = R32(UTLB_A(i)); c->utlb_d[i] = R32(UTLB_D(i)); }
  for (uint32_t i = 0; i < 4; i++) { c->itlb_a[i] = R32(ITLB_A(i)); c->itlb_d[i] = R32(ITLB_D(i)); }
}

uint32_t wdt_sync(uint32_t csr) {
  R16(WTCSR) = (uint16_t)(0xA500u | (csr & (W_WTIT | W_RSTS)) | 7u);   /* TME 0: mode, CKS 7 */
  R16(WTCNT) = 0x5AFD;
  (void)R8(WTCNT);
  R16(WTCSR) = (uint16_t)(0xA500u | W_TME | (csr & (W_WTIT | W_RSTS)) | 7u);
  while (R8(WTCNT) != 0xFF) ;
  return pmc();
}

void wdt_stop(void) {
  const uint32_t ws = R8(WTCSR);
  R16(WTCSR) = (uint16_t)(0xA500u | (ws & 7u));
  (void)R8(WTCSR);
}
