/* rstx.c -- M3 verification: the CPU-only reset (an exception with SR.BL = 1, SOC.md 2.2 / 2.3 / 4.3.7) taken while the
 * memory system has a transaction in flight, on the whole chip (rm) in lockstep (SOC.md 6.2: the core's own reset
 * requests are EXC 0x020 records).  Run WITHOUT --ccr: the boot stub then leaves CCR alone and the caches keep their
 * contents across the CPU-only reset (SOC.md 2.3: CCR, IC / OC V and U, tags, data and SQ data are held), so the
 * effect of each in-flight access can be read after the re-entry.  The boot stub itself runs again -- it rewrites the
 * BSC (MCR, the SDRAM mode set, refresh), DMAOR / CHCR0 / CHCR2, the SCIF, BRCR, PMCR1, QACR0 / 1 -- while those
 * transactions are still on the bus.
 * One phase per boot; the phase counter lives in P2 memory (G_W(0)).  Phase p sets up and triggers; boot p + 1 checks
 * (SOC.md 2.3's rows "LFB / WBB / WTB / SQ busy: an access killed after its claim completes -- its fill validates the
 * line, its store bytes, WTB store, MOVCA.L allocation, SQ burst stand", "a BSC bus cycle in progress: held
 * (completes)", "DMAC: held"):
 *   0 an SQ burst (PREF)            -> memory holds the burst
 *   1 a P2 store (WTB)              -> memory holds it
 *   2 a P1 store miss (allocation)  -> the line is valid and dirty with the store; its OCBP writes it
 *   3 OCBP of a dirty line          -> memory holds the line
 *   4 PREF of an absent line        -> the OC address array shows the line valid, clean
 *   5 MOVCA.L of an absent line     -> valid, dirty, the word readable; the OCBP writes it
 *   6 a DMAC channel-1 transfer (cycle steal, 2 KB) -> it completes after the re-entry; the destination is the source
 *   7 TAS.B                         -> the byte has bit 7 set in memory
 *   8 SQ stores without the PREF    -> the SQ data is held: a PREF after the re-entry writes it
 *   9 JSR to a cold P1 routine (its IC fill)  -> the IC address array shows the line valid
 *  10 a dirty line evicted by its alias's load (the WBB) -> memory holds it
 * Every boot also checks EXPEVT = 0x020.  Output: "RSTX phase=<p> <what> got=<v> want=<v> ok=<0|1>", then
 * "RSTX summary bad=<n>", "RSTX done". */
#include "m3.h"

extern void t_pref(int go, uint32_t a);
extern void t_st(int go, uint32_t a, uint32_t v);
extern void t_ocbp(int go, uint32_t a);
extern void t_movca(int go, uint32_t a, uint32_t v);
extern void t_tas(int go, uint32_t a);
extern void t_ill(int go);
extern void t_jsr(int go, uint32_t fn);
extern void t_evict(int go, uint32_t x, uint32_t v, uint32_t y);
extern uint32_t r_cold(void);
extern uint32_t x_ld32_p2(uint32_t a);

#define X_PA(p) (TR_PA + 0x3000u + 0x40u * (uint32_t)(p))   /* OC entries 0x180 + 2p: clear of the start-up code's literals */
#define NBAD G_W(1)
static void rep(int ph, const char *w, uint32_t got, uint32_t want) {
  int ok = got == want;
  if (!ok) NBAD = NBAD + 1;
  printf("RSTX phase=%d %s got=%08lx want=%08lx ok=%d\n", ph, w, (unsigned long)got, (unsigned long)want, ok);
}
static uint32_t early_aa;
static uint32_t ocaa(uint32_t pa) { (void)pa; return early_aa; }   /* read at the start of main (before printf) */
static uint32_t tagv(uint32_t pa, int u, int v) { return (pa & 0x1FFFFC00u) | ((uint32_t)u << 1) | (uint32_t)v; }
#define SRC_PA (DR_PA)
#define DST_PA (DR_PA + 0x8000u)

static void check(int p) {
  uint32_t x = X_PA(p);
  switch (p) {
  case 0: rep(p, "sq burst", R32(P2(x) + 28), 0x50000007u); break;
  case 1: rep(p, "p2 store", R32(P2(x) + 4), 0x51000001u); break;
  case 2:
    rep(p, "store miss aa", ocaa(x), tagv(x, 1, 1));
    rep(p, "store miss p1", R32(P1(x) + 8), 0x52000002u);
    ocbp(P1(x));
    rep(p, "store miss ocbp", R32(P2(x) + 8), 0x52000002u);
    break;
  case 3: rep(p, "ocbp", R32(P2(x) + 12), 0x53000003u); break;
  case 4: rep(p, "pref aa", ocaa(x), tagv(x, 0, 1)); break;
  case 5:
    rep(p, "movca aa", ocaa(x), tagv(x, 1, 1));
    rep(p, "movca p1", R32(P1(x)), 0x55000005u);
    ocbp(P1(x));
    rep(p, "movca ocbp", R32(P2(x)), 0x55000005u);
    break;
  case 6: {
    uint32_t t0 = pmc();
    while (R32(DMATCR(1)) != 0 && pmc() - t0 < 2000000u) ;
    rep(p, "dma tcr", R32(DMATCR(1)), 0);
    rep(p, "dma te", (R32(CHCR(1)) >> 1) & 1u, 1);
    uint32_t bad = 0;
    for (uint32_t o = 0; o < 2048; o += 4) if (R32(P2(DST_PA) + o) != R32(P2(SRC_PA) + o)) bad++;
    rep(p, "dma data words bad", bad, 0);
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
    break;
  }
  case 7: rep(p, "tas", R8(P2(x) + 3), 0x80u | 0x17u); break;
  case 8:
    pref(0xE0000000u | (x & 0x03FFFFE0u));
    rep(p, "sq held + pref", R32(P2(x) + 20), 0x58000005u);
    break;
  case 9: {
    uint32_t fn = (uint32_t)(uintptr_t)r_cold;
    rep(p, "ic fill icaa", x_ld32_p2(ICAA((fn >> 5) & 0xFFu)) & 0x1FFFFC01u, (fn & 0x1FFFFC00u) | 1u);
    break;
  }
  case 10: rep(p, "wbb", R32(P2(x) + 16), 0x5A00000Au); break;
  default: break;
  }
}

static void trigger(int p) {
  uint32_t x = X_PA(p);
  ocbp(P1(x)); ocbp(P1(x) + 0x4000u);
  for (int w = 0; w < 8; w++) R32(P2(x) + 4u * (uint32_t)w) = 0xEE000000u | ((uint32_t)p << 8) | (uint32_t)w;
  switch (p) {
  case 0: {
    uint32_t sqa = 0xE0000000u | (x & 0x03FFFFE0u);
    for (int w = 0; w < 8; w++) R32(sqa + 4u * (uint32_t)w) = 0x50000000u + (uint32_t)w;
    t_pref(0, 0); t_pref(1, sqa);
    break;
  }
  case 1: t_st(0, 0, 0); t_st(1, P2(x) + 4, 0x51000001u); break;
  case 2: t_st(0, 0, 0); t_st(1, P1(x) + 8, 0x52000002u); break;
  case 3: R32(P1(x) + 12) = 0x53000003u; t_ocbp(0, 0); t_ocbp(1, P1(x)); break;
  case 4: t_pref(0, 0); t_pref(1, P1(x)); break;
  case 5: t_movca(0, 0, 0); t_movca(1, P1(x), 0x55000005u); break;
  case 6:
    for (uint32_t o = 0; o < 2048; o += 4) { R32(P2(SRC_PA) + o) = 0x56000000u ^ (o * 0x9E3779B1u); R32(P2(DST_PA) + o) = 0; }
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
    R32(SAR(1)) = SRC_PA; R32(DAR(1)) = DST_PA; R32(DMATCR(1)) = 2048 / 4;
    t_ill(0);
    R32(CHCR(1)) = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3) | CH_DE;   /* cycle steal, longwords */
    t_ill(1);
    break;
  case 7: R8(P2(x) + 3) = 0x17; t_tas(0, 0); t_tas(1, P1(x) + 3); break;
  case 8: {
    uint32_t sqa = 0xE0000000u | (x & 0x03FFFFE0u);
    for (int w = 0; w < 8; w++) R32(sqa + 4u * (uint32_t)w) = 0x58000000u + (uint32_t)w;
    t_st(0, 0, 0); t_st(1, sqa + 20, 0x58000005u);
    break;
  }
  case 9: {
    uint32_t fn = (uint32_t)(uintptr_t)r_cold;
    ccr_set(CCR_KOS | CCR_ICI);
    t_jsr(0, 0); t_jsr(1, fn);
    break;
  }
  case 10: t_evict(0, 0, 0, 0); t_evict(1, P1(x) + 16, 0x5A00000Au, P1(x) + 0x4000u); break;
  default: break;
  }
  printf("RSTX phase=%d: the trigger returned (no reset)\n", p);
  NBAD = NBAD + 1;
}

int main(void) {
  int ph = (int)G_W(0);
  if (ph > 0) early_aa = R32(OCAA((P1(X_PA(ph - 1)) >> 5) & 0x1FFu)) & 0x1FFFFC03u;
  printf("RSTX boot phase=%d expevt=%03lx ccr=%08lx\n", ph, (unsigned long)R32(EXPEVT), (unsigned long)ccr_get());
  if (ph == 0) { NBAD = 0; ccr_set(CCR_KOS | CCR_ICI | CCR_OCI); R32(QACR0) = 0x0C; R32(QACR1) = 0x0C; }
  else {
    rep(ph - 1, "expevt", R32(EXPEVT), 0x020);
    rep(ph - 1, "ccr held", ccr_get() & 0xFFFFu, CCR_KOS);
    R32(QACR0) = 0x0C; R32(QACR1) = 0x0C;          /* the stub wrote 4 */
    check(ph - 1);
  }
  if (ph >= 11) {
    printf("RSTX summary bad=%lu\n", (unsigned long)NBAD);
    printf("RSTX done\n");
    return NBAD ? 1 : 0;
  }
  G_W(0) = (uint32_t)ph + 1;
  trigger(ph);
  return 1;
}
