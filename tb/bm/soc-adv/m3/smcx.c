/* smcx.c -- M3 verification: self-modifying code through every write path, the I-cache flash-cleared and the patched
 * routine called back to back (smcx_asm.S, from P2), on the whole chip (rm) in lockstep.  The new code must run (the
 * manual's procedure: write, write back, ICI, jump): the IC fill of the line must come after the write in bus order --
 * SOC.md 3.3's BIU rules (a read of the line waiting in the WBB waits for it; the WTB drains before any read; a read
 * waits for a store-queue burst on the bus) and the BSC's in-order bus.
 *   p1  a P1 store (dirty OC line) + OCBWB (the WBB)   p2  a P2 store (the WTB)   sq  a store-queue burst (PREF)
 *   wt  a P0 store with CCR.WT = 1 (the WTB)           dma a DMAC channel-1 copy of the new line, TE waited for
 * each with k = 0 .. 12 cycles between the write and the ICI.  Output: "SMCX <path> k=<k> got=<v> want=<v> ok=<0|1>",
 * "SMCX summary cases=<n> bad=<n>", "SMCX done". */
#include "m3.h"

extern uint32_t x_smc_p1(uint32_t fn, uint32_t word, uint32_t k);
extern uint32_t x_smc_p2(uint32_t fn, uint32_t word, uint32_t k);
extern uint32_t x_smc_sq(uint32_t fn, uint32_t sqa, uint32_t k);
extern uint32_t x_smc_wt(uint32_t fn, uint32_t word, uint32_t k);
extern uint32_t r_smc0(void), r_smc1(void), r_smc2(void), r_smc3(void);

static int ncase, nbad;
static void rep(const char *c, int k, uint32_t got, uint32_t want) {
  int ok = got == want;
  ncase++; if (!ok) nbad++;
  printf("SMCX %s k=%d got=%08lx want=%08lx ok=%d\n", c, k, (unsigned long)got, (unsigned long)want, ok);
}
#define WORD(imm) (0x000BE000u | ((uint32_t)(imm) & 0xFFu))   /* MOV #imm,R0 ; RTS */
static void reset_fn(uint32_t fn) {                  /* the routine back to MOV #1 in memory, not cached anywhere */
  ocbp(P1(fn));
  R32(P2(fn)) = WORD(1);
  ccr_set(CCR_KOS | CCR_WT | CCR_ICI);
  (void)((uint32_t (*)(void))(uintptr_t)fn)();       /* warm: the IC holds the old line (1) */
}

int main(void) {
  printf("SMCX start\n");
  ccr_set(CCR_KOS | CCR_WT);
  R32(QACR0) = 0x0C; R32(QACR1) = 0x0C;
  for (uint32_t k = 0; k <= 12; k += 2) {
    uint32_t fn = (uint32_t)(uintptr_t)r_smc0;
    reset_fn(fn);
    rep("p1", (int)k, x_smc_p1(fn, WORD(2 + k), k), 2 + k);
    fn = (uint32_t)(uintptr_t)r_smc1;
    reset_fn(fn);
    rep("p2", (int)k, x_smc_p2(fn, WORD(3 + k), k), 3 + k);
    fn = (uint32_t)(uintptr_t)r_smc2;
    reset_fn(fn);
    {
      uint32_t sqa = 0xE0000000u | (P1(fn) & 0x03FFFFE0u), base = P2(fn) & ~31u;
      for (int w = 0; w < 8; w++) R32(sqa + 4u * (uint32_t)w) = R32(base + 4u * (uint32_t)w);   /* the line as it is */
      R32(sqa + (P1(fn) & 31u)) = WORD(4 + k);
      rep("sq", (int)k, x_smc_sq(fn, sqa, k), 4 + k);
    }
    fn = (uint32_t)(uintptr_t)r_smc3;
    reset_fn(fn);
    rep("wt", (int)k, x_smc_wt(fn, WORD(5 + k), k), 5 + k);
  }
  /* the DMAC copies the new line in; TE is waited for, then ICI and the call */
  for (int k = 0; k < 4; k++) {
    uint32_t fn = (uint32_t)(uintptr_t)r_smc0, src = DR_PA + 0x100u;
    reset_fn(fn);
    for (int w = 0; w < 8; w++) R32(P2(src) + 4u * (uint32_t)w) = R32(P2(fn & ~31u) + 4u * (uint32_t)w);
    R32(P2(src) + (fn & 31u)) = WORD(9 + k);
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
    R32(SAR(1)) = src; R32(DAR(1)) = PHYS(fn) & ~31u; R32(DMATCR(1)) = 1;
    R32(CHCR(1)) = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(4) | CH_DE;
    while (R32(DMATCR(1)) != 0) ;
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
    rep("dma stale", k, ((uint32_t (*)(void))(uintptr_t)fn)(), 1);   /* the IC still holds the old line */
    ccr_set(CCR_KOS | CCR_WT | CCR_ICI);
    rep("dma", k, ((uint32_t (*)(void))(uintptr_t)fn)(), (uint32_t)(9 + k));
  }
  printf("SMCX summary cases=%d bad=%d\n", ncase, nbad);
  printf("SMCX done\n");
  return nbad ? 1 : 0;
}
