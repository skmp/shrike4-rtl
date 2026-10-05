/* ddtrst.c -- the M6 independent verification's program (agents/reports/integration/M6-verify.md), the M6 gate's
 * regression test since M6-fix (agents/reports/integration/M6-fix.md; case 0 is M6-verify F2's repro): a WDT manual
 * reset while Holly's PVR-DMA runs through DDT (soc_tb --ddt --resets 1, rm).  SOC.md 2.3's manual column: the DMAC's CHCR / DMAOR are 0,
 * SAR / DAR / DMATCR held, the DDT queues empty, a unit in progress completes; the stub (tools/boot/stub/stub.S) writes
 * CHCR0 / CHCR2 / DMAOR 0x8201 again before the program restarts.  Holly (the board) is not reset by the SH4's manual
 * reset: its DMA goes on posting units.  Between the reset and the stub's DMAOR write DDT = 0, so the shared pins are
 * DREQ / DRAK / DACK (SOC.md 3.3.3), at CHCR0's reset polarity (RL = AL = 0: active high, idle low).
 *   boot 1 (CASE 0): a 4 KB PVR-DMA RAM -> texture memory starts; the WDT (CKS 5, WTCNT 0xFE, RSTS 1: manual) fires
 *          within ~2 kIcyc, in the middle of the transfer (128 units, 251 Icyc apart)
 *   boot 1 (CASE 1, control): the PVR-DMA ends before the WDT is armed
 *   boot 2: the WDT stopped; DMAOR / CHCR0 / SAR0 / PDST reported; the PVR-DMA's end awaited (PDST 0, <= 1.5 M Icyc);
 *          then a fresh 1 KB PVR-DMA must run to its end with its data right (the DDT path recovered)
 * Output: "RX boot=<n> ...", "RX done fails=<n>".  The run's own verdict (a DDT protocol error, a time-out) is in
 * soc_tb's status.  M6-fix: the board takes BAVL# only as a falling edge after its DBREQ# (tb/dc_board/dc_ddt.h), so
 * the DRAK0 / DACK0 idle level between the reset and the stub's DMAOR write is no grant: the unit whose DBREQ# went out
 * in that window is given up (one time-out, 8 words lost), every other unit lands -- before, the level was a grant and
 * the stub's own bus cycles fell in Holly's turn Bclk (a DDT protocol error).  The DMA's source buffer lives outside
 * .bss (at 0x0CE04000, beside PERS) so the reboot's crt0 does not clear it under the DMA: boot 2 checks the texture
 * words (m6/check.py: 1024 less 8 per time-out). */
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
#define PDSTARD  0xA05F7CF4u
#define PDLEND   0xA05F7CF8u
#define P2(p)    (((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu) | 0xA0000000u)
#define PHYS(p)  ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define TEXP     0xA4600000u
#ifndef CASE
#define CASE 0
#endif

typedef struct { uint32_t magic, boots, t_arm, pdst_at_arm, sar0_at_arm; } per_t;
#define PERS ((volatile per_t *)0xACE02000u)
#define MAGIC 0x52535458u                                  /* "RSTX" */

#define bufA ((uint32_t *)0xACE04000u)                    /* 4 KB outside .bss: it survives the reboot (P2) */
static uint32_t pat(uint32_t k, uint32_t i) { return (k * 0x01000193u) ^ (i * 0x9E3779B1u) ^ 0x3C000000u; }
static int fails;

static void h_any(irq_t code, irq_context_t *ctx, void *d) {
  (void)d;
  printf("RX unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
  R32(0xB8000000u) = 0x3FE;
  for (;;) {}
}
static void pvr_start(uint32_t k, uint32_t len) {
  volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *t = (volatile uint32_t *)TEXP;
  for (uint32_t i = 0; i < len / 4; i++) { a[i] = pat(k, i); t[i] = 0; }
  R32(ISTNRM) = 1u << 11;
  R32(PDAPRO) = 0x6702007Fu;
  R32(PDSTAP) = 0x04600000u; R32(PDSTAR) = PHYS(bufA); R32(PDLEN) = len; R32(PDDIR) = 0; R32(PDTSEL) = 0;
  R32(PDEN) = 1; R32(PDST) = 1;
}
static uint32_t tex_good(uint32_t k, uint32_t len) {
  volatile uint32_t *t = (volatile uint32_t *)TEXP;
  uint32_t g = 0;
  for (uint32_t i = 0; i < len / 4; i++) g += t[i] == pat(k, i);
  return g;
}

int main(void) {
  bm_default = h_any;
  imask(15);
  if (PERS->magic != MAGIC) {
    PERS->magic = MAGIC; PERS->boots = 1;
    pvr_start(1, 4096);
    if (CASE == 1) {
      const uint32_t t0 = pmc();
      while ((R32(PDST) & 1) && pmc() - t0 < 1000000u) {}
      spin(500);
    } else spin(300);
    PERS->pdst_at_arm = R32(PDST); PERS->sar0_at_arm = R32(SAR(0));
    printf("RX boot=1 case=%d armed: PDST=%lu SAR0-start=%ld DMAOR=%08lx\n", CASE, (unsigned long)PERS->pdst_at_arm,
           (long)(PERS->sar0_at_arm - PHYS(bufA)), (unsigned long)R32(DMAOR));
    R16(WTCSR) = 0xA500;
    R16(WTCNT) = 0x5AFE;
    R16(WTCSR) = 0xA5E5;                                 /* TME, WT/IT, RSTS (manual), CKS 5 */
    (void)R8(WTCSR);
    const uint32_t t0 = pmc();
    while (pmc() - t0 < 200000u) {}
    printf("RX boot=1 no reset\n");
    R32(0xB8000000u) = 0x3FD;
    for (;;) {}
  }
  R16(WTCSR) = 0xA500;                                   /* the WDT counts on after its reset: stop it */
  (void)R8(WTCSR);
  PERS->boots++;
  const uint32_t dmaor = R32(DMAOR), c0 = R32(CHCR(0)), s0 = R32(SAR(0)), tc0 = R32(DMATCR(0)), pd = R32(PDST);
  printf("RX boot=%lu EXPEVT=%03lx DMAOR=%08lx CHCR0=%08lx SAR0-start=%ld DMATCR0=%lu PDST=%lu\n",
         (unsigned long)PERS->boots, (unsigned long)R32(0xFF000024u), (unsigned long)dmaor, (unsigned long)c0,
         (long)(s0 - PHYS(bufA)), (unsigned long)tc0, (unsigned long)pd);
  const uint32_t t0 = pmc();
  while ((R32(PDST) & 1) && pmc() - t0 < 1500000u) {}
  const uint32_t tw = pmc() - t0;
  spin(500);
  const uint32_t g1 = tex_good(1, 4096);
  printf("RX boot=%lu first dma: PDST=%lu after %lu Icyc, texture words right %lu of 1024, SAR0-start=%ld PDSTARD-start=%ld\n",
         (unsigned long)PERS->boots, (unsigned long)(R32(PDST) & 1), (unsigned long)tw, (unsigned long)g1,
         (long)(R32(SAR(0)) - PHYS(bufA)), (long)(R32(PDSTARD) - PHYS(bufA)));
  if (R32(PDST) & 1) fails++;
  if (CASE == 1 && g1 != 1024) fails++;
  /* recovery: a fresh 1 KB PVR-DMA through DDT */
  pvr_start(2, 1024);
  const uint32_t t1 = pmc();
  while ((R32(PDST) & 1) && pmc() - t1 < 1000000u) {}
  spin(500);
  const uint32_t g2 = tex_good(2, 1024);
  printf("RX boot=%lu second dma: PDST=%lu words right %lu of 256 SAR0-start=%ld CHCR0=%08lx DMAOR=%08lx\n",
         (unsigned long)PERS->boots, (unsigned long)(R32(PDST) & 1), (unsigned long)g2, (long)(R32(SAR(0)) - PHYS(bufA)),
         (unsigned long)R32(CHCR(0)), (unsigned long)R32(DMAOR));
  if ((R32(PDST) & 1) || g2 != 256 || R32(SAR(0)) != PHYS(bufA) + 1024) fails++;
  PERS->magic = 0;
  printf("RX done fails=%d\n", fails);
  return 0;
}
