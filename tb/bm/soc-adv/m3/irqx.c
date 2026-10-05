/* irqx.c -- M3 verification: interrupts and exceptions taken at every point of a code block larger than the I-cache
 * with operand-cache misses, dirty write-backs, PREF / OCBWB / OCBP, store-queue bursts, TRAPA and misaligned loads (the
 * handler skips them) -- on the whole chip (rm), in lockstep.
 * The oracle is model-free: the block (irqx_asm.S, generated) is run NP times from the same memory image twice,
 * run 0 with interrupts masked, run 1 with TMU0 underflowing every 6..21 Pphi/4 ticks at random phases (the handler
 * reprograms TCOR0 from an LCG) and a DMAC channel-3 transfer stealing cycles elsewhere; the two memory images, the
 * per-pass register results and the exception counts must be equal.  Output: "IRQX run=<r> sum=<hex> exc=<n>
 * irqs=<n>", "IRQX compare ok=<0|1> passes_bad=<n>", "IRQX done". */
#include "m3.h"

#ifndef NP
#define NP 8
#endif
extern void blk_run(uint32_t *regs);
#define RG_LO (TR_PA + 0x10000u)
#define RG_HI (TR_PA + 0x30000u)
static uint32_t regs[8];
static uint32_t xr[2][NP];

static void init_region(void) {
  flush_range(P1(RG_LO), RG_HI - RG_LO);
  for (uint32_t a = RG_LO; a < RG_HI; a += 4) R32(P2(a)) = (a * 0x2545F491u) ^ 0xA5A5A5A5u;
}
static uint32_t checksum(void) {
  flush_range(P1(RG_LO), RG_HI - RG_LO);
  uint32_t s = 0;
  for (uint32_t a = RG_LO; a < RG_HI; a += 4) s = s * 31u + R32(P2(a));
  return s;
}
static void run(int r) {
  init_region();
  uint32_t walk = P1(TR_PA + 0x28000u);
  uint32_t e0 = g_nexc, i0 = g_nirq;
  if (r) {
    /* a DMAC channel-3 transfer elsewhere (DR), cycle steal, longwords */
    (void)R32(CHCR(3)); R32(CHCR(3)) = 0;
    R32(SAR(3)) = DR_PA; R32(DAR(3)) = DR_PA + 0x40000u; R32(DMATCR(3)) = 0x8000u / 4u;
    R32(CHCR(3)) = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3) | CH_DE;
    tmu_reset();
    R16(IPRA) = 0xF000;
    R32(TCOR0) = 9; R32(TCNT0) = 9; R16(TCR0) = UNIE;
    g_tmu_rand = 1; g_irq_quiet = 1;
    R8(TSTR) = 1;
    imask(0);
  }
  for (int p = 0; p < NP; p++) {
    regs[0] = P1(TR_PA + 0x10000u); regs[1] = P1(TR_PA + 0x14000u); regs[2] = P1(TR_PA + 0x18000u);
    regs[3] = P1(TR_PA + 0x1C000u); regs[4] = 0xE0000000u | ((TR_PA + 0x20000u) & 0x03FFFFE0u);
    regs[5] = P1(TR_PA + 0x21001u); regs[6] = walk; regs[7] = 0x1234u + (uint32_t)p * 0x9E37u;
    blk_run(regs);
    walk = regs[6];
    xr[r][p] = regs[7];
  }
  if (r) { imask(15); tmu_reset(); g_tmu_rand = 0; (void)R32(CHCR(3)); R32(CHCR(3)) = 0; }
  printf("IRQX run=%d sum=%08lx exc=%lu irqs=%lu walk=%08lx\n", r, (unsigned long)checksum(),
         (unsigned long)(g_nexc - e0 - (g_nirq - i0)), (unsigned long)(g_nirq - i0), (unsigned long)walk);
}

int main(void) {
  printf("IRQX start np=%d\n", NP);
  R32(QACR0) = 0x0C; R32(QACR1) = 0x0C;
  g_skip = 1;                                          /* a misaligned load: resume behind it */
  uint32_t s[2], ex[2];
  for (int r = 0; r < 2; r++) {
    uint32_t e0 = g_nexc, i0 = g_nirq;
    run(r);
    s[r] = checksum();
    ex[r] = (g_nexc - e0) - (g_nirq - i0);
  }
  int bad = 0;
  for (int p = 0; p < NP; p++) if (xr[0][p] != xr[1][p]) { bad++; printf("IRQX pass=%d regs %08lx / %08lx\n", p, (unsigned long)xr[0][p], (unsigned long)xr[1][p]); }
  int ok = s[0] == s[1] && ex[0] == ex[1] && !bad;
  printf("IRQX compare sum %08lx / %08lx exc %lu / %lu passes_bad=%d ok=%d\n", (unsigned long)s[0], (unsigned long)s[1],
         (unsigned long)ex[0], (unsigned long)ex[1], bad, ok);
  printf("IRQX done\n");
  return ok ? 0 : 1;
}
