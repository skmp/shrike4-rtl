/* coh.c -- M3 verification: the CPU's memory paths on the whole chip (rm: IC, OC, SQ, BIU, BSC, SDRAM), a random mix
 * of every access path and cache operation over a working set that thrashes the operand cache, with a model-free
 * oracle.
 *
 * Working set: 64 lines, 16 operand-cache indices x 4 aliases (0x4000 apart, so every OC index in play holds one of 4
 * lines: dirty victims, write-backs, refills of a line whose write-back is still in the WBB).  Every operation keeps
 * the working set coherent from the CPU's point of view, so a shadow copy (`sh`, the CPU-visible bytes) predicts every
 * load:
 *   P1 / P0 / P3 loads and stores of 1, 2, 4, 8 bytes (8: FMOV with FPSCR.SZ = 1, cached quadword); P0 / P3 with
 *   CCR.WT = 1 are write-through; P2 (uncached, 8 bytes: two longword bus cycles) after an OCBP of the line; OCBWB,
 *   OCBP, PREF; OCBI after an OCBWB; MOVCA.L then the rest of the line; TAS.B (purge, locked read / write); a store-
 *   queue burst (8 longwords, PREF) after an OCBP of its line; bursts of back-to-back accesses to one line.
 * Every load is compared with the shadow at once ("COH MISMATCH ..."), and at the end every line is written back and
 * read through P2.  --lockstep checks each access against Sh4Sys as well.
 * Phases (CCR from P2): 0 KOS's CCR (P1 copy-back, P0 / P3 copy-back); 1 CB = 0, WT = 1 (P1 / P0 / P3
 * write-through); 2 CB = 1, WT = 1 (P1 copy-back, P0 / P3 write-through) with TMU0 interrupts at random phases;
 * 3 as 0 with OIX = 1 (the OC indexed by VA[25] and VA[12:5]) entered after a whole-cache flush.
 * An incoherent epilogue (lockstep is its only oracle): P2 stores to lines held dirty, OCBI of dirty lines, P1 reads of
 * stale lines, a store-queue burst over a cached line, MOVCA.L without the rest of the line; its reads are summed.
 * Output: "COH phase=<p> ops=<n> loads=<n> errors=<n>" per phase, "COH final errors=<n> sum=<hex>", "COH incoh
 * sum=<hex>", "COH done". */
#define MIX_TAG "COH"
#ifdef CASE                                    /* coh_c<HEX>: bits 15..0 the seed; bit 16: 8 lines, 2 OC indices x 4;
                                                  bit 17: a refresh every 3 x 16 Bcyc (RTCOR 3, the stub's 0x5E) */
#define MIX_SEED (0x1234567u ^ ((uint32_t)(CASE) * 0x9E3779B1u))
#if (CASE) & 0x10000
#define NL 8
#define MIX_NIDX 2
#endif
#endif
#include "mix.h"
extern void flush_oc_16k(uint32_t dummy_p1, uint32_t ccr);   /* coh_asm.S: read 16 KB (every OC index), CCR <- ccr */

static void incoherent(void) {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) {
    uint32_t pa = lpa(i);
    R32(P1(pa)) = 0x11110000u + (uint32_t)i;         /* dirty in the OC */
    R32(P2(pa)) = 0x22220000u + (uint32_t)i;         /* memory under it */
    sum = sum * 31u + R32(P1(pa));                   /* the cache: 1111 */
    sum = sum * 31u + R32(P2(pa));                   /* memory: 2222 */
    if (i & 1) ocbi(P1(pa)); else ocbp(P1(pa));      /* OCBI drops the dirty 1111; OCBP writes it over 2222 */
    sum = sum * 31u + R32(P1(pa));
    /* a store-queue burst over a line held clean in the OC: the OC keeps the old line */
    uint32_t w[8];
    for (int x = 0; x < 8; x++) w[x] = 0x33330000u + (uint32_t)(i * 8 + x);
    (void)R32(P1(pa) + 4u);
    sq_line(0xE0000000u | (pa & 0x03FFFFE0u), w);
    sum = sum * 31u + R32(P1(pa) + 4u);              /* stale (the OC's) */
    sum = sum * 31u + R32(P2(pa) + 4u);              /* the burst's */
    ocbi(P1(pa));
    sum = sum * 31u + R32(P1(pa) + 4u);              /* refetched: the burst's */
    /* MOVCA.L alone: the other words of the line are whatever the allocation left */
    movca(P1(pa), 0x44440000u + (uint32_t)i);
    sum = sum * 31u + R32(P1(pa));
    ocbp(P1(pa));
    sum = sum * 31u + R32(P2(pa));
  }
  printf("COH incoh sum=%08lx\n", (unsigned long)sum);
}

int main(void) {
  printf("COH start nops=%d\n", NOPS);
  R32(QACR0) = 0x0C; R32(QACR1) = 0x0C;               /* the store queues write area 3 */
#if defined(CASE) && ((CASE) & 0x20000)
  R16(RTCOR) = 0xA503;                                 /* refresh storm: every 48 Bcyc */
  printf("COH rtcor=%04x rtcsr=%04x\n", R16(RTCOR), R16(RTCSR));
#endif
  init_lines();
  for (int ph = 0; ph < 4; ph++) {
    uint32_t e0 = errors, l0 = nloads;
    if (ph == 0) { ccr_set(CCR_KOS); wt_p0 = 0; }
    if (ph == 1) { ccr_set(CCR_ICE | CCR_OCE | CCR_WT); wt_p0 = 1; }
    if (ph == 2) {
      ccr_set(CCR_ICE | CCR_OCE | CCR_CB | CCR_WT); wt_p0 = 1;
      tmu_reset();
      R16(IPRA) = 0xF000;                                /* TMU0 at level 15 */
      R32(TCOR0) = 11; R32(TCNT0) = 11; R16(TCR0) = UNIE;
      g_tmu_rand = 1; g_irq_quiet = 1;
      R8(TSTR) = 1;
      imask(0);
    }
    if (ph == 3) {
      imask(15); tmu_reset(); g_tmu_rand = 0;
      flush_oc_16k(P1(0x0C500000u), CCR_KOS | CCR_OIX);
      wt_p0 = 0;
    }
    for (int n = 0; n < NOPS; n++) op(ph);
    printf("COH phase=%d ccr=%08lx ops=%d loads=%lu errors=%lu irqs=%lu\n", ph, (unsigned long)ccr_get(), NOPS,
           (unsigned long)(nloads - l0), (unsigned long)(errors - e0), (unsigned long)g_nirq);
    final_check("final");
  }
  incoherent();
  printf("COH total errors=%lu\n", (unsigned long)errors);
  printf("COH done\n");
  return errors ? 1 : 0;
}
