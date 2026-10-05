/* uartx.c -- M4 verification (M4-verify F3 / F6; the M4 gate's since M4-fix): the SCIF console line against soc_tb's
 * UART monitor (SOC.md 7.4: it decodes TxD2 at the bit time --uart-bit gives, or at the one the chip's SCBRR2 /
 * SCSMR2 set, and cross-checks v1_scif's tap; a framing error fails the run).  Built per case:
 *   uartx_c0   one line at the stub's rate (SCBRR2 = 0x0C: 32 x 13 = 416 Pcyc per bit, 115200): run with the chip's bit
 *              time and with --uart-bit 416; a SCIF at the wrong rate (a wrong clock enable, a wrong baud generator)
 *              then disagrees with the tap
 *   uartx_c1   one line at 115200, then a WDT manual reset in the middle of a frame (soc_tb --resets 1), then a line
 *              after the stub re-initialised the SCIF: the monitor drops the cut frame
 *   uartx_c2   one line at 115200, then SCBRR2 = 0 (1.5625 Mbaud) and another line: the monitor follows the rate
 *   uartx_c3   (M4-fix) WDT manual resets swept across one frame's end (soc_tb --resets 40): boot 1 prints line A,
 *              measures one frame from its SCFTDR2 write to TEND (the end of its stop bit, serial.md 3.2) and calibrates
 *              the reset's time after a WDT synchronisation (rstfly's method, 6 counts); boot j + 2 writes the byte 'a' + j so that the reset
 *              comes d = -1500 + 100 j Icyc after that frame's end (1664 Icyc per bit).  Before the stop bit's middle
 *              the frame is cut (the monitor drops it, the tap never reports it); from the middle to the end the monitor
 *              has decoded it but the tap (the end of the stop bit) has not: soc_tb must count it as a frame the reset
 *              cut in its stop bit, not a disagreement; after the end both have it.  m4/check.py wants the transcript
 *              line A, '#', then some of the letters in order, and at least one frame cut in its stop bit
 * Output: sim-control "UARTX ..." lines; the SCIF text itself goes to soc_tb's --scif file (m4/check.py compares it).
 * */
#include "m4v.h"
#ifndef CASE
#define CASE 0
#endif

static void scif_putc(char c) {
  uint32_t t0 = pmc();
  while (!(R16(SCFSR2) & 0x20) && pmc() - t0 < 2000000u) ;
  R8(SCFTDR2) = (uint8_t)c;
  R16(SCFSR2) = (uint16_t)(R16(SCFSR2) & 0xFF9F);        /* clear TEND, TDFE (KOS scif_write) */
}
static void scif_puts(const char *s) { while (*s) scif_putc(*s++); }
static void scif_drain(void) { uint32_t t0 = pmc(); while (!(R16(SCFSR2) & 0x40) && pmc() - t0 < 4000000u) ; }

#if CASE == 3
#define C3_MAGIC 0x55584333u                               /* "UXC3" */
#define C3_LO    (-1500)
#define C3_HI    300
#define C3_STEP  100
enum { P3_MAGIC = 0, P3_J, P3_TREF, P3_TS, P3_F };

/* wdt_sync (m4v.c) with a longer run to the overflow: WTCNT from 0xF8, synchronised at the F9 -> FA count (a regular
 * edge), the overflow 6 counts (~24,600 Icyc, CKS 7) later -- longer than one frame (~17,400 Icyc from the write) */
static uint32_t wdt_sync6(void) {
  R16(WTCSR) = (uint16_t)(0xA500u | W_WTIT | W_RSTS | 7u);
  R16(WTCNT) = 0x5AF8;
  (void)R8(WTCNT);
  R16(WTCSR) = (uint16_t)(0xA500u | W_TME | W_WTIT | W_RSTS | 7u);
  while (R8(WTCNT) != 0xFA) ;
  return pmc();
}

/* scenario j (0: the calibration's timestamp loop); the reset ends it */
static __attribute__((noreturn)) void c3_boot(int j) {
  volatile uint32_t *P = PER;
  P[P3_J] = (uint32_t)j; P[P3_TS] = 0;
  const uint32_t t0 = wdt_sync6();
  if (j == 0) for (;;) P[P3_TS] = pmc() - t0;
  const int d = C3_LO + (j - 1) * C3_STEP;               /* the reset d Icyc after the frame's end */
  const uint32_t tw = P[P3_TREF] - P[P3_F] - (uint32_t)d;
  while (pmc() - t0 < tw) ;
  R8(SCFTDR2) = (uint8_t)('a' + j - 1);
  R16(SCFSR2) = (uint16_t)(R16(SCFSR2) & 0xFF9F);
  for (;;) {}
}

int main(void) {
  imask(15);
  volatile uint32_t *P = PER;
  const int n = (C3_HI - C3_LO) / C3_STEP + 1;
  if (CAP->boots == 1 || P[P3_MAGIC] != C3_MAGIC) {
    P[P3_MAGIC] = C3_MAGIC;
    scif_puts("UARTX line A at 115200\n");
    scif_drain();
    const uint32_t tw = pmc();                             /* one frame: the write to TEND */
    R8(SCFTDR2) = '#';
    R16(SCFSR2) = (uint16_t)(R16(SCFSR2) & 0xFF9F);
    while (!(R16(SCFSR2) & 0x40) && pmc() - tw < 100000u) ;
    P[P3_F] = pmc() - tw;
    printf("UARTX c3 start scenarios=%d frame=%lu\n", n, (unsigned long)P[P3_F]);
    c3_boot(0);
  }
  const int j = (int)P[P3_J];
  if (j == 0) P[P3_TREF] = P[P3_TS];
  printf("UARTX c3 j=%d boot=%lu expevt=%03lx d=%d tref=%lu\n", j, (unsigned long)CAP->boots, (unsigned long)CAP->expevt,
         j ? C3_LO + (j - 1) * C3_STEP : 0, (unsigned long)P[P3_TREF]);
  if (j < n) c3_boot(j + 1);
  P[P3_MAGIC] = 0;
  printf("UARTX done\n");
  return 0;
}
#else
int main(void) {
  imask(15);
  if (CAP->boots > 1) {
    printf("UARTX boot=%lu expevt=%03lx\n", (unsigned long)CAP->boots, (unsigned long)CAP->expevt);
    scif_puts("UARTX line C after the reset\n");
    scif_drain();
    printf("UARTX done\n");
    return 0;
  }
  scif_puts("UARTX line A at 115200\n");
  scif_drain();
#if CASE == 2
  R16(SCSCR2) = 0; R8(SCBRR2) = 0; (void)R8(SCBRR2);
  { uint32_t t0 = pmc(); while (pmc() - t0 < 2000) ; }
  R16(SCSCR2) = 0x30;
  scif_puts("UARTX line B at 1.5625 Mbaud\n");
  scif_drain();
#endif
#if CASE == 1
  (void)wdt_sync(W_WTIT | W_RSTS);                      /* the reset ~4096 Icyc later: inside the frames below */
  scif_puts("VWXYZ");
  for (;;) {}
#endif
  printf("UARTX done\n");
  return 0;
}
#endif
