/* prio.c -- M1a adversarial test: simultaneous on-chip interrupt sources through the chip (v1_soc's irq_src_t
 * assembly, v1_intc's resolver, the units' request levels, the core's take).
 *
 * Seven sources are made pending at once under IMASK 15: TMU0 / TMU1 / TMU2 underflow (one TSTR write starts all three
 * on the same tick), SCI TXI (TDRE with TIE), SCIF TXI (TDFE with TIE), WDT ITI (interval overflow), REF RCMI (refresh
 * compare match).  Then IMASK drops to 0 and the handler clears each source as it is taken (the source named by
 * INTEVT, with a read-back) and returns.  The order of INTEVT codes must follow the IPR levels, and within one level
 * hm Table 19.5's default priority: TMU0 > TMU1 > TMU2 > RTC > SCI1 > SCIF > WDT > REF.  The lockstep checker takes
 * each IRQ's code from the RTL (it checks the level against IMASK only), so it does not check which source wins.
 * Output: "PRIO <case> order=<code>,<code>,..." */
#include "adv.h"

static void setup_sources(void) {
  tmu_reset();
  R32(TCOR0) = 0xFFFFFFFFu; R32(TCOR1) = 0xFFFFFFFFu; R32(TCOR2) = 0xFFFFFFFFu;
  R32(TCNT0) = 2; R32(TCNT1) = 2; R32(TCNT2) = 2;
  R16(TCR0) = UNIE; R16(TCR1) = UNIE; R16(TCR2) = UNIE;
  R8(TSTR) = 7;                                   /* all three start on one tick */
  while ((R16(TCR0) & R16(TCR1) & R16(TCR2) & UNF) == 0) ;
  R8(TSTR) = 0;
  R8(STBCR) = 0x02;                               /* SCI running (the stub stops it: 0x03) */
  (void)R8(STBCR);
  R8(SCSCR1) = 0xA0;                              /* SCI: TIE, TE (TDRE = 1 after init) */
  R16(SCSCR2) = 0x00B0;                           /* SCIF: TIE, TE, RE (TDFE = 1: the FIFO is empty) */
  R16(WTCSR) = 0xA500;                            /* WDT stopped */
  R16(WTCNT) = 0x5AF8;
  R16(WTCSR) = 0xA580;                            /* TME, interval mode, CKS 0 */
  while (!(R8(WTCSR) & 0x08)) ;                   /* IOVF */
  R16(RTCSR) = 0xA550;                            /* REF: CMIE, CKS = CKIO / 16 (the stub's) */
  while (!(R16(RTCSR) & 0x80)) ;                  /* CMF */
}

static void cleanup(void) {
  R16(IPRA) = 0; R16(IPRB) = 0; R16(IPRC) = 0;
  tmu_reset();
  R8(SCSCR1) = 0x00;
  R16(SCSCR2) = 0x0030;
  R16(WTCSR) = 0xA500;
  R16(RTCSR) = 0xA510;
  R8(STBCR) = 0x03;
  (void)R16(IPRC);
}

static void run(const char *name, uint16_t ipra, uint16_t iprb, uint16_t iprc) {
  set_sr(0x400000F0u);
  setup_sources();
  R16(IPRA) = ipra; R16(IPRB) = iprb; R16(IPRC) = iprc;
  (void)R16(IPRC);
  g_clr_unie = 1;
  g_noreadback = 0;
  g_resume = 0;
  clrlog();
  set_sr(0x40000000u);                            /* IMASK 0 */
  spin(200);
  set_sr(0x400000F0u);
  printf("PRIO %s order=", name);
  for (uint32_t i = 0; i < g_nlog; i++) printf(i ? ",%03lx" : "%03lx", (unsigned long)g_log[i].intevt);
  printf(" n=%lu\n", (unsigned long)g_nlog);
  cleanup();
}

int main(void) {
  printf("prio: start\n");
  /* IPRA: TMU0 [15:12] TMU1 [11:8] TMU2 [7:4] RTC [3:0]; IPRB: WDT [15:12] REF [11:8] SCI [7:4]; IPRC: SCIF [7:4] */
  run("equal5", 0x5550, 0x5550, 0x0050);
  run("ascend", 0x2460, 0xCE80, 0x00A0);          /* TMU0 2 TMU1 4 TMU2 6 SCI 8 SCIF 10 WDT 12 REF 14 */
  run("descend", 0xEC00 | 0xA0, 0x2460, 0x0080);  /* TMU0 14 TMU1 12 TMU2 10 SCI 6 SCIF 8 WDT 2 REF 4 */
  run("tmu2top", 0x7780, 0x7770, 0x0070);         /* TMU2 8, the rest 7 */
  run("sciffirst", 0x3330, 0x3330, 0x0040);       /* SCIF 4, the rest 3 */
  printf("prio: done\n");
  return 0;
}
