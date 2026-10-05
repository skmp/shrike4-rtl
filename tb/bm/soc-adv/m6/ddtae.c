/* ddtae.c -- the M6 independent verification's program (agents/reports/integration/M6-verify.md), the M6 gate's
 * regression test since M6-fix (agents/reports/integration/M6-fix.md): DMAOR changes while Holly's PVR-DMA runs
 * through DDT (soc_tb --ddt, rm, --lockstep).  M6-fix: with DDT cleared (case 1) the pins are DRAK0 / DACK0 at their
 * idle level, BAVL# low without a falling edge is no grant (tb/dc_board/dc_ddt.h), so the unit whose DBREQ# went out
 * then is given up (one time-out) rather than handshaken over the chip's own traffic (M6-verify F2).  SOC.md 9.30 (c): "A DTR the chip drops (AE / NMIF, hm
 * 14.3.6 ...; DMAOR.DDT cleared): the board gives the unit up after 400,000 Icyc [P]".  One case per build (-DCASE=n):
 *   0  AE: channel 1 runs one class-B unit (SAR1 in area 7, auto request, cycle steal: no access, AE at its end, spec
 *      3.8) 3,000 Icyc into a 4 KB PVR-DMA; AE is cleared (DMAOR read, then written with AE 0) 5,000 Icyc later
 *   1  DDT off: DMAOR <- 0x0201 (DDT 0, PR 10, DME 1) 3,000 Icyc into the PVR-DMA, back to 0x8201 5,000 Icyc later
 *   2  DME off: DMAOR <- 0x8200 (DDT 1, DME 0) 3,000 Icyc into the PVR-DMA, back to 0x8201 5,000 Icyc later
 *   3  CHCR0 rewritten by the CPU in DDT mode (ignored, dmac2 T1) every ~100 Icyc all through the PVR-DMA (control)
 * The program waits for PDST 0 (<= 3 M Icyc) and reports the texture words, SAR0, CHCR0, DMAOR; soc_tb's status line
 * has the board's verdict (time-outs, protocol errors).  Output "AE case=<n> ...", "AE done fails=<n>" (fails: the
 * DMA did not end, or DMAOR not restored). */
#include "m1b.h"

#define ISTNRM   0xA05F6900u
#define PDSTAP   0xA05F7C00u
#define PDSTAR   0xA05F7C04u
#define PDLEN    0xA05F7C08u
#define PDDIR    0xA05F7C0Cu
#define PDTSEL   0xA05F7C10u
#define PDEN     0xA05F7C14u
#define PDST     0xA05F7C18u
#define PDAPRO   0xA05F7C80u
#define P2(p)    (((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu) | 0xA0000000u)
#define PHYS(p)  ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define TEXP     0xA4600000u
#ifndef CASE
#define CASE 0
#endif

static uint32_t bufA[1024] __attribute__((aligned(32)));
static uint32_t bufD[64] __attribute__((aligned(32)));
static uint32_t pat(uint32_t i) { return (i * 0x9E3779B1u) ^ 0x7E000000u; }
static void h_any(irq_t code, irq_context_t *ctx, void *d) {
  (void)d;
  printf("AE unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
  R32(0xB8000000u) = 0x3FE;
  for (;;) {}
}
static void wait_icyc(uint32_t n) { const uint32_t t = pmc(); while (pmc() - t < n) {} }

int main(void) {
  bm_default = h_any;
  imask(15);
  ocbp_range(bufA, sizeof bufA); ocbp_range(bufD, sizeof bufD);
  volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *t = (volatile uint32_t *)TEXP;
  for (int i = 0; i < 1024; i++) { a[i] = pat((uint32_t)i); t[i] = 0; }
  const uint32_t dmaor0 = R32(DMAOR);
  R32(ISTNRM) = 1u << 11;
  R32(PDAPRO) = 0x6702007Fu;
  R32(PDSTAP) = 0x04600000u; R32(PDSTAR) = PHYS(bufA); R32(PDLEN) = 4096; R32(PDDIR) = 0; R32(PDTSEL) = 0;
  R32(PDEN) = 1;
  const uint32_t t0 = pmc();
  R32(PDST) = 1;
  uint32_t mid = 0, midc = 0;
  if (CASE == 3) {
    while ((R32(PDST) & 1) && pmc() - t0 < 3000000u) { R32(CHCR(0)) = 0; wait_icyc(100); }
  } else {
    wait_icyc(3000);
    if (CASE == 0) {
      (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
      R32(SAR(1)) = 0x1C000000u; R32(DAR(1)) = PHYS(bufD); R32(DMATCR(1)) = 1;
      R32(CHCR(1)) = 0x5441u;                              /* DM SM +, auto, cycle steal, 32 B, DE: SAR1 in area 7 */
    } else if (CASE == 1) R32(DMAOR) = 0x0201u;
    else R32(DMAOR) = 0x8200u;
    wait_icyc(300);
    mid = R32(DMAOR); midc = R32(CHCR(1));
    wait_icyc(4700);
    if (CASE == 0) { (void)R32(DMAOR); R32(DMAOR) = dmaor0; }     /* AE read as 1, then written 0 */
    else R32(DMAOR) = dmaor0;
  }
  const uint32_t tr = pmc() - t0;
  while ((R32(PDST) & 1) && pmc() - t0 < 3000000u) {}
  const uint32_t tend = pmc() - t0;
  spin(500);
  int good = 0;
  for (int i = 0; i < 1024; i++) good += t[i] == pat((uint32_t)i);
  const int ended = !(R32(PDST) & 1);
  const int fails = !ended || R32(DMAOR) != dmaor0;
  printf("AE case=%d DMAOR mid=%08lx CHCR1 mid=%08lx restored at %lu | PDST ended=%d at %lu Icyc words right %d of 1024 "
         "SAR0-start=%ld CHCR0=%08lx DMATCR0=%lu DMAOR=%08lx\n", CASE, (unsigned long)mid, (unsigned long)midc,
         (unsigned long)tr, ended, (unsigned long)tend, good, (long)(R32(SAR(0)) - PHYS(bufA)),
         (unsigned long)R32(CHCR(0)), (unsigned long)R32(DMATCR(0)), (unsigned long)R32(DMAOR));
  (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
  R32(ISTNRM) = 1u << 11; R32(PDEN) = 0;
  printf("AE done fails=%d\n", fails);
  return 0;
}
