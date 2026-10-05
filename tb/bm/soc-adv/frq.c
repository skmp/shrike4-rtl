/* frq.c -- M1a adversarial test (found while testing the size rule): the Pclk landing (SOC.md 3.1.5) against the
 * CPG's relock settle (SOC.md 2.1, timers-clock 4.4.2).  A FRQCR write that turns PLL1 on (0x0A0A -> 0x0E0A) stops
 * bce / pce while the WDT counts the stabilisation time.  The write takes effect at its landing; a read of FRQCR strobed
 * in that landing pce is presented to the CPG one pce later -- which the settle withholds -- while its ack keeps the
 * strobe's schedule.  Every read must return the FRQCR the program wrote (hm 10: FRQCR reads back what was written).
 * Output: "FRQ k=<nops> first=<read after 0x0A0A> r1=<first read after 0x0E0A> r2=<second>" */
#include "adv.h"

typedef void (*frq_t)(uint32_t *);
extern frq_t frq_tab[9];

int main(void) {
  printf("frq: start\n");
  for (int k = 0; k < 9; k++) {
    uint32_t o[3];
    R16(WTCSR) = 0xA500;                  /* WDT stopped, CKS 0 (Pphi / 32): the settle counts from WTCNT */
    R16(WTCNT) = 0x5AF8;                  /* 8 counts to overflow: ~1 k Icyc per relock phase */
    (void)R8(WTCSR);
    frq_tab[k](o);
    printf("FRQ k=%d first=%04lx r1=%04lx r2=%04lx\n", k, (unsigned long)(o[0] & 0xFFFF), (unsigned long)(o[1] & 0xFFFF),
           (unsigned long)(o[2] & 0xFFFF));
  }
  printf("frq: done\n");
  return 0;
}
