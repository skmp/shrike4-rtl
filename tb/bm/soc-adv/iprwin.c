/* iprwin.c -- tb/bm/soc-adv (from the M1a verification): the Pclk landing window and the interrupt acceptance it opens.
 *
 * TMU0 has an underflow pending (UNF = 1, UNIE = 1, IPRA TMU0 = 5) under IMASK 15.  A posted Pclk store, then k NOPs,
 * then LDC SR lowers IMASK to 3 (the request would come at s + 9 / 10, SOC.md 3.4).  Modes, each k = 0..19:
 *   raise    IPRA 0 -> 5 (the source starts masked by level 0): legal whenever it is taken
 *   rdback   IPRA 5 -> 0 with a read-back of IPRA right after the store (hm 19.3's rule): never taken
 *   unfclr   TCR0 <- UNIE (UNF cleared): the source goes away when the write lands; a take before that is legal for
 *            the model (level 5 > IMASK 3)
 *   lower    IPRA 5 -> 0 with no read-back: the RTL's INTC sees the old level until the write lands (strobe + 8,
 *            SOC.md 3.1.5 Landing) + 6; hm 19.3 warns of "erroneous interrupt acceptance" here.  The lockstep model's
 *            IPR copy takes the store when it steps it; the checker accepts the old level for an interrupt taken inside
 *            the store's landing window (SOC.md 6.2, Sh4Sys::intc_window; M1a-verify F1, fixed in M1a-fix)
 *   lowerbusy  lower behind a longword TCOR1 store: the IPRA store's strobe is the latest after its ack, so this is
 *            the widest window the structure allows (the lockstep allowance must cover it, and no more)
 * Output: "IPRW <mode> k=<k> taken=<n> intevt=<code> at=<(SPC - LDC SR) / 2> adds=<r8>". */
#include "adv.h"

typedef uint32_t (*pw_t)(uint32_t, uint32_t, uint32_t, uint32_t);
extern pw_t pw_tab[20], pr_tab[20], pb_tab[20];
extern uint32_t pw_ldc_tab[20], pr_ldc_tab[20], pb_ldc_tab[20];

static void tmu0_pending(uint16_t ipra) {
  tmu_reset();
  R16(IPRA) = ipra;
  R32(TCOR0) = 0xFFFFFFFFu;
  R32(TCNT0) = 2;
  R16(TCR0) = UNIE;                 /* Pphi / 4 */
  R8(TSTR) = 1;
  while (!(R16(TCR0) & UNF)) ;
  R8(TSTR) = 0;
  (void)R8(TSTR);
  (void)R16(IPRA);
}

static void run(const char *name, int rb, uint32_t addr, uint16_t ipra0, uint16_t val) {
  for (int k = 0; k < 20; k++) {
    tmu0_pending(ipra0);
    g_clr_unie = 1;                 /* the handler clears UNF and UNIE (and reads TCR0 back): one entry at most */
    g_noreadback = 0;
    clrlog();
    const uint32_t adds = (rb == 2 ? pb_tab : rb ? pr_tab : pw_tab)[k](addr, val, 0x40000030u, TCOR1);
    const uint32_t ldc = (rb == 2 ? pb_ldc_tab : rb ? pr_ldc_tab : pw_ldc_tab)[k];
    if (g_nlog)
      printf("IPRW %s k=%d taken=%lu intevt=%03lx at=%ld adds=%lu\n", name, k, (unsigned long)g_nlog,
             (unsigned long)g_log[0].intevt, (long)((int32_t)(g_log[0].spc - ldc) / 2), (unsigned long)adds);
    else
      printf("IPRW %s k=%d taken=0 adds=%lu\n", name, k, (unsigned long)adds);
  }
}

int main(void) {
  printf("iprwin: start\n");
  run("raise", 0, IPRA, 0x0000, 0x5000);
  run("rdback", 1, IPRA, 0x5000, 0x0000);
  run("unfclr", 0, TCR0, 0x5000, UNIE);
  run("lower", 0, IPRA, 0x5000, 0x0000);
  run("lowerbusy", 2, IPRA, 0x5000, 0x0000);
  tmu_reset();
  R16(IPRA) = 0;
  printf("iprwin: done\n");
  return 0;
}
