/* sfres.c -- tb/bm/soc-adv/m4 (M4-fix, WI-SOC; M4-verify F1, whose probe it extends): SB_SFRES (Holly's system reset,
 * 0x005F6890 <- 0x7611; board.md 3.10) under soc_tb --resets 1.  SOC.md 2.2: the RESET pin's sources are "board:
 * power-on, SB_SFRES"; SOC.md 7.4: '"reset requested" (SB_SFRES or a WDT reset) ends the run unless --resets N allows
 * it'.  With --resets the board resets Holly and soc_tb holds the RESET pin low with SCK2 high: a pin power-on reset,
 * after which the boot stub runs again (its power-on path: EXPEVT = 0).  Without --resets the run ends "system reset
 * requested (SB_SFRES)" (tb/mk/v1_soc.mk checks both).
 * Boot 1 gives four registers a value each reset kind treats differently and writes SB_SFRES:
 *   FRQCR     0x060A (CKOEN 0: no PLL relock, v1_cpg)  a pin reset reloads it from the MD straps: 0x0E0A (SOC.md 2.2);
 *                                                      a WDT power-on or a manual reset holds it (SOC.md 2.3)
 *   SAR1      0x0C123440                                a power-on reset clears it, a manual one holds it (SOC.md 2.3)
 *   IML6ERR   0x00000002                                Holly's reset: 0 (board.md 3.10; its interrupt controller)
 *   SDSTAW    0x0C123400                                Holly's reset, then the BIOS-left preset 0x0CFF0000 again (the
 *                                                      boot stub stands for the BIOS: board.md 5.3, SOC.md 2.2)
 * Boot 2 reports "SFRES boot=2 <reg> set=<hex> got=<hex> want=<hex> OK|FAIL" for each and EXPEVT (0x000: a power-on
 * reset), then "SFRES done fails=<n>", and exits 0.  A boot 1 that is not reset prints "SFRES no reset" and exits 0x3F.
 * Checked by m4/check.py --progs sfres (soc_tb's own verdict: the RESET pin (SB_SFRES) in the reset list). */
#include "m4v.h"

#define SAR1_V   0x0C123440u
#define SDSTAW   0xA05F6810u
#define P_MAGIC  0
#define P_FRQ    1
#define P_SAR    2
#define P_IML    3
#define P_SDS    4
#define MAGIC    0x53465253u                               /* "SFRS" */

static int fails;
static void row(const char *n, uint32_t set, uint32_t got, uint32_t want) {
  const int ok = got == want;
  printf("SFRES boot=%lu %s set=%08lx got=%08lx want=%08lx %s\n", (unsigned long)CAP->boots, n, (unsigned long)set,
         (unsigned long)got, (unsigned long)want, ok ? "OK" : "FAIL");
  if (!ok) fails++;
}

int main(void) {
  imask(15);
  volatile uint32_t *P = PER;
  if (CAP->boots > 1 && P[P_MAGIC] == MAGIC) {
    P[P_MAGIC] = 0;
    row("EXPEVT", 0, CAP->expevt, 0x000);
    row("FRQCR", P[P_FRQ], CAP->frqcr, 0x0E0A);            /* m4v_early's copy: before the program touched it */
    row("SAR1", P[P_SAR], R32(SAR(1)), 0);
    row("IML6ERR", P[P_IML], R32(IML6ERR), 0);
    row("SDSTAW", P[P_SDS], R32(SDSTAW), 0x0CFF0000u);
    printf("SFRES done fails=%d\n", fails);
    return 0;
  }
  R16(FRQCR) = 0x060A;
  R32(SAR(1)) = SAR1_V;
  R32(IML6ERR) = 0x00000002u;
  R32(SDSTAW) = 0x0C123400u;
  P[P_FRQ] = R16(FRQCR); P[P_SAR] = R32(SAR(1)); P[P_IML] = R32(IML6ERR); P[P_SDS] = R32(SDSTAW);
  P[P_MAGIC] = MAGIC;
  printf("SFRES boot=1 frqcr=%04lx sar1=%08lx iml6err=%08lx sdstaw=%08lx: writing SB_SFRES\n", (unsigned long)P[P_FRQ],
         (unsigned long)P[P_SAR], (unsigned long)P[P_IML], (unsigned long)P[P_SDS]);
  R32(0xA05F6890u) = 0x7611;
  for (volatile int i = 0; i < 200000; i++) ;
  P[P_MAGIC] = 0;
  printf("SFRES no reset\n");
  return 0x3F;
}
