/* M4-fix (WI-SOC): M4-verify's program (agents/reports/integration/M4-verify.md), the M4 gate's regression test
 * since M4-fix (tb/mk/v1_soc.mk v1-soc-m4-sub; the verdict: m4/check.py --progs). */
/* rstfly.c -- M4 verification: WDT manual resets that land on a transaction in flight, through the boot stub (soc_tb
 * --resets N, rm: IDEAL_MEM = 0).  SOC.md 2.3, manual column: "a BSC bus cycle in progress completes"; "LFB / WBB /
 * WTB / SQ busy: an access killed after its claim completes -- its fill validates the line, its store bytes, WTB
 * store, MOVCA.L allocation, SQ burst stand; one killed before its claim has no effect"; "DMAC ... SAR, DAR, DMATCR
 * held ... a unit in progress completes first"; "IC and OC V, U / tags and data: held"; CCR 0.
 * Boot 1 calibrates: the WDT (CKS 7) is synchronised to a regular count edge (m4v.c wdt_sync) and the reset's time
 * after it measured by a timestamp loop (PMCTR1 - t0 stored to P2 until the reset).  Every later boot checks what the
 * previous boot's transaction left (from memory, the held DMAC registers and the OC arrays m4v_early captured before
 * crt0's OCI), then starts its own: the WDT synchronised again, a spin to (reset time - lead), the transaction, the
 * timestamp loop.  The leads sweep each transaction across the reset:
 *   sq    an SQ burst (PREF) of 32 new bytes to a RAM line holding 32 old ones: all old or all new
 *   ocw   8 stores to a P1 copy-back line (a store-miss fill, then hits) and OCBWB: memory all old or all new; the OC
 *         entry (V, U, tag, data) consistent with it (a dirty line: memory old, data a prefix of the stores; a clean
 *         valid line: data = memory; no valid line: memory old)
 *   dma   DMAC channel 1, 8 units of 32 bytes, auto-request, cycle steal: CHCR1 = 0; n = 8 - DMATCR1 units done;
 *         SAR1 / DAR1 advanced by exactly n units; the first n destination units new, the rest old
 *   g2w   a 32-bit write to an AICA channel register over G2: old or new
 *   fmov  a 64-bit FMOV store to P2 RAM: all old or all new
 * Every re-boot also checks EXPEVT 0x020, the WDT's WTCSR (WOVF set, TME / WT/IT / RSTS / CKS held: 0xF7) and CCR 0.
 * Built with -DPOR (rstfly_por): the same sweep with WDT power-on resets (RSTS = 0), whose SOC.md 2.3 column differs:
 * a bus cycle in progress is aborted, the buffers go idle, the DMAC's SAR / DAR / DMATCR / CHCR are 0, OC V / U clear,
 * EXPEVT 0x000, WTCSR 0xD7; memory may then hold a torn line (reported, not failed); the point is that the chip re-boots
 * through the stub's power-on path (PALL, MRS) after SDRAM traffic was cut, with the SDRAM monitor clean.
 * Output (sim-control): "RFLY j=<n> kind=<k> lead=<l> start=<t> reset=<t> rel=<t> <result> OK|FAIL <why>", then
 * "RFLY done scenarios=<n> fails=<f>".  Checked by m4/check.py --progs rstfly / rstfly_por.
 * Built with -DKINDS=<mask> (rstfly_c<mask>): only those kinds (rstfly_c8: the DMA sweep alone, the planted-bug check of
 * make v1-soc-mutants). */
#include "m4v.h"

#define MAGIC 0x52464C59u                                  /* "RFLY" */
enum { K_CAL = 0, K_SQ, K_OCW, K_DMA, K_G2W, K_FMOV, K_N };
static const char *const KN[K_N] = {"cal", "sq", "ocw", "dma", "g2w", "fmov"};
/* lead sweep per kind: lo, hi, step (Icyc from the transaction's start to the calibrated reset) */
static const int SW[K_N][3] = {{0, 0, 1}, {-24, 60, 3}, {-24, 120, 4}, {-40, 480, 20}, {-40, 400, 20}, {-24, 60, 3}};
#ifdef POR
#define RST_MODE W_WTIT                                     /* RSTS = 0: a WDT power-on reset */
#define W_EXPEVT 0x000u
#define W_WTCSR  0xD7u
#else
#define RST_MODE (W_WTIT | W_RSTS)
#define W_EXPEVT 0x020u
#define W_WTCSR  0xF7u
#endif
#ifndef KINDS
#define KINDS 0x3E                                         /* the kinds to run (bit k) */
#endif

/* PER words */
#define P_MAGIC 0
#define P_J     1                                          /* the scenario running / just run */
#define P_TREF  2                                          /* the calibrated reset time after wdt_sync's return */
#define P_TS    3                                          /* the timestamp loop's word */
#define P_START 4                                          /* the transaction's start after t0 (~0: never started) */
#define P_KIND  5
#define P_LEAD  6
#define P_FAILS 7
#define P_TIOVF 8
#define P_NSC   9

static void h_any(irq_t code, irq_context_t *ctx, void *d) {
  (void)d;
  printf("RFLY unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
  R32(SIM_EXIT) = 0x3FE;
  for (;;) {}
}

static int nsc(void) {                                     /* scenarios after the calibration */
  int n = 0;
  for (int k = 1; k < K_N; k++) if (KINDS >> k & 1) n += (SW[k][1] - SW[k][0]) / SW[k][2] + 1;
  return n;
}
static void sc_of(int j, int *kind, int *lead) {           /* j = 0: the calibration */
  int i = j - 1;
  if (j <= 0) { *kind = K_CAL; *lead = 0; return; }
  for (int k = 1; k < K_N; k++) {
    if (!(KINDS >> k & 1)) continue;
    const int n = (SW[k][1] - SW[k][0]) / SW[k][2] + 1;
    if (i < n) { *kind = k; *lead = SW[k][1] - i * SW[k][2]; return; }   /* from the earliest start to the latest */
    i -= n;
  }
  *kind = K_CAL; *lead = 0;
}

static uint32_t oldw(int j, int i) { return 0x5A000000u | ((uint32_t)j << 8) | (uint32_t)i; }
static uint32_t neww(int j, int i) { return 0xA5000000u | ((uint32_t)j << 8) | (uint32_t)i; }
static uint32_t sq_pa(int j)   { return 0x0C700000u + 32u * (uint32_t)(j & 255); }
static uint32_t ocw_pa(int j)  { return 0x0C710000u + 32u * (uint32_t)(j & 127); }
static uint32_t dma_src(void)  { return 0x0C740000u; }
static uint32_t dma_dst(int j) { return 0x0C760000u + 256u * (uint32_t)(j & 63); }
static uint32_t g2_reg(int j)  { return 0xA0700000u + 0x80u * (uint32_t)(j & 63) + 0x08u; }
static uint32_t fm_pa(int j)   { return 0x0C780000u + 8u * (uint32_t)(j & 511); }

static __attribute__((noreturn)) void ts_loop(uint32_t t0) {
  for (;;) PER[P_TS] = pmc() - t0;
}

static void st64(uint32_t a, uint32_t w0, uint32_t w1) {  /* FMOV DR0,@a with SZ = 1 */
  __asm__ __volatile__("lds %1,fpul\n\tfsts fpul,fr0\n\tlds %2,fpul\n\tfsts fpul,fr1\n\t"
                       "fschg\n\tfmov dr0,@%0\n\tfschg" : : "r"(a), "r"(w0), "r"(w1) : "fr0", "fr1", "fpul", "memory");
}

/* classify n words at P2 address a against the old / new patterns: 0 old, 1 new, -1 neither */
static int cls(uint32_t a, int j, int n) {
  int o = 1, w = 1;
  for (int i = 0; i < n; i++) { const uint32_t v = R32(P2A(a) + 4u * (uint32_t)i); if (v != oldw(j, i)) o = 0; if (v != neww(j, i)) w = 0; }
  return o ? 0 : w ? 1 : -1;
}

/* the check of the previous boot's scenario; returns 1 when it passed */
static int check_prev(int j, int kind, int lead) {
  const uint32_t st = PER[P_START], ts = PER[P_TS];
  char why[160]; why[0] = 0;
  char res[96]; res[0] = 0;
  int ok = 1;
  if (CAP->expevt != W_EXPEVT) { ok = 0; sprintf(why, "EXPEVT %03lx, want %03lx", (unsigned long)CAP->expevt, (unsigned long)W_EXPEVT); }
  if (CAP->wtcsr != W_WTCSR) { ok = 0; sprintf(why + strlen(why), " WTCSR %02lx, want %02lx", (unsigned long)CAP->wtcsr, (unsigned long)W_WTCSR); }
#ifdef POR
  const int por = 1;
#else
  const int por = 0;
#endif
  if (CAP->ccr != 0) { ok = 0; sprintf(why + strlen(why), " CCR %08lx, want 0", (unsigned long)CAP->ccr); }
  switch (kind) {
  case K_SQ: {
    const int c = cls(sq_pa(j), j, 8);
    sprintf(res, "mem=%s", c == 0 ? "old" : c == 1 ? "new" : "TORN");
    if (c < 0 && !por) { ok = 0; sprintf(why + strlen(why), "%s", " SQ burst torn"); }
    /* (start = -1 only says the reset came before the store of the start time behind the PREF) */
    break;
  }
  case K_OCW: {
    const uint32_t pa = ocw_pa(j), e = ((pa | 0x80000000u) >> 5) & 511u;
    const int c = cls(pa, j, 8);
    const uint32_t a = CAP->oca[0];
    const int v = a & 1, u = (a >> 1) & 1, tm = CAP->n_ocw == 1 && CAP->ocw_idx[0] == e && (a >> 10) == (pa >> 10);
    int dn = 0, dprefix = 1, dnew = 0;                    /* data words: new ones, a prefix of the stores? */
    for (int i = 0; i < 8; i++) {
      const uint32_t d = CAP->ocd[0][i];
      if (d == neww(j, i)) { dn++; if (dnew != i) dprefix = 0; dnew++; }
      else if (d != oldw(j, i)) dprefix = 0;
    }
    int deqm = 1;
    for (int i = 0; i < 8; i++) if (CAP->ocd[0][i] != R32(P2A(pa) + 4u * (uint32_t)i)) deqm = 0;
    sprintf(res, "mem=%s oc=%s%s%s new=%d", c == 0 ? "old" : c == 1 ? "new" : "TORN", tm && v ? "V" : "-", tm && v && u ? "U" : "",
            tm ? "" : "(other)", dn);
    if (por) {                                             /* POR: V and U clear (the tag / data held) */
      if (v || u) { ok = 0; sprintf(why + strlen(why), "%s", " OC V / U not cleared by the power-on reset"); }
    } else {
    if (c < 0) { ok = 0; sprintf(why + strlen(why), "%s", " write-back torn"); }
    if (tm && v && u) {
      if (c != 0) { ok = 0; sprintf(why + strlen(why), "%s", " a dirty line but memory not old"); }
      if (!dprefix) { ok = 0; sprintf(why + strlen(why), "%s", " dirty data not a prefix of the stores over the fill"); }
    } else if (tm && v) {
      if (!deqm) { ok = 0; sprintf(why + strlen(why), "%s", " a clean valid line whose data differs from memory"); }
    } else {
      if (c != 0) { ok = 0; sprintf(why + strlen(why), "%s", " no valid line but memory not old"); }
    }
    }
    break;
  }
  case K_DMA: {
    const uint32_t chcr = R32(CHCR(1)), sar = R32(SAR(1)), dar = R32(DAR(1)), tcr = R32(DMATCR(1));
    const uint32_t n = 8u - tcr, src = dma_src(), dst = dma_dst(j);
    int unit_bad = 0;
    for (uint32_t u = 0; u < 8 && n <= 8; u++) {
      int o = 1, w = 1;
      for (int i = 0; i < 8; i++) {
        const uint32_t x = R32(P2A(dst) + 32u * u + 4u * (uint32_t)i);
        if (x != oldw(j, (int)(8 * u) + i)) o = 0;
        if (x != neww(j, (int)(8 * u) + i)) w = 0;
      }
      if (u < n ? !w : !o) unit_bad++;
    }
    sprintf(res, "done=%lu chcr=%lx sar=+%lx dar=+%lx dmaor=%lx", (unsigned long)n, (unsigned long)chcr,
            (unsigned long)(sar - src), (unsigned long)(dar - dst), (unsigned long)R32(DMAOR));
    if (por) {                                             /* POR: SAR DAR DMATCR CHCR 0 [P] */
      if (chcr || sar || dar || tcr) { ok = 0; sprintf(why + strlen(why), "%s", " DMAC channel 1 not 0 after the power-on reset"); }
      break;
    }
    if (tcr > 8) { ok = 0; sprintf(why + strlen(why), "%s", " DMATCR1 out of range"); }
    if (chcr != 0) { ok = 0; sprintf(why + strlen(why), "%s", " CHCR1 not 0 after the reset"); }
    if (sar != src + 32u * n || dar != dst + 32u * n) { ok = 0; sprintf(why + strlen(why), "%s", " SAR1 / DAR1 disagree with DMATCR1"); }
    if (unit_bad) { ok = 0; sprintf(why + strlen(why), " %d destination units disagree with DMATCR1", unit_bad); }
    break;
  }
  case K_G2W: {
    const uint32_t x = R32(g2_reg(j)) & 0xFFFF;
    sprintf(res, "reg=%lx", (unsigned long)x);
    if (x != (0x1000u + (uint32_t)j) && x != (0x2000u + (uint32_t)j)) { ok = 0; sprintf(why + strlen(why), "%s", " AICA register neither old nor new"); }
    break;
  }
  case K_FMOV: {
    const uint32_t w0 = R32(P2A(fm_pa(j))), w1 = R32(P2A(fm_pa(j)) + 4);
    const int o = w0 == oldw(j, 0) && w1 == oldw(j, 1);
    const int n1 = (w0 == neww(j, 0) && w1 == neww(j, 1)) || (w0 == neww(j, 1) && w1 == neww(j, 0));
    sprintf(res, "mem=%s", o ? "old" : n1 ? "new" : "TORN");
    if (!o && !n1 && !por) { ok = 0; sprintf(why + strlen(why), "%s", " 64-bit store torn"); }
    break;
  }
  default:
    sprintf(res, "tref=%lu tiovf=%lu", (unsigned long)ts, (unsigned long)PER[P_TIOVF]);
    break;
  }
  printf("RFLY j=%d kind=%s lead=%d start=%ld reset=%lu rel=%ld %s %s%s\n", j, KN[kind], lead,
         st == ~0u ? -1L : (long)st, (unsigned long)ts, st == ~0u ? -99999L : (long)ts - (long)st, res, ok ? "OK" : "FAIL",
         why);
  return ok;
}

/* set up and run scenario j (does not return: the reset ends it) */
static __attribute__((noreturn)) void run(int j) {
  int kind, lead;
  sc_of(j, &kind, &lead);
  PER[P_J] = (uint32_t)j; PER[P_KIND] = (uint32_t)kind; PER[P_LEAD] = (uint32_t)lead; PER[P_START] = ~0u; PER[P_TS] = 0;
  WATCH->n_ocw = 0; WATCH->n_icw = 0;
  const uint32_t tgt = PER[P_TREF] - (uint32_t)lead;
  uint32_t a = 0, b = 0, t0, t;
  switch (kind) {
  case K_SQ:
    a = sq_pa(j);
    for (int i = 0; i < 8; i++) R32(P2A(a) + 4u * (uint32_t)i) = oldw(j, i);
    R32(QACR0) = ((a >> 26) & 7u) << 2; R32(QACR1) = ((a >> 26) & 7u) << 2;
    b = 0xE0000000u | (a & 0x03FFFFE0u);
    for (int i = 0; i < 8; i++) R32(b + 4u * (uint32_t)i) = neww(j, i);   /* the SQ the address's bit 5 selects */
    (void)R32(P2A(a));
    t0 = wdt_sync(RST_MODE);
    while ((t = pmc() - t0) < tgt) ;
    __asm__ __volatile__("pref @%0" : : "r"(b) : "memory");
    break;
  case K_OCW: {
    a = ocw_pa(j);
    for (int i = 0; i < 8; i++) R32(P2A(a) + 4u * (uint32_t)i) = oldw(j, i);
    volatile uint32_t *p = (volatile uint32_t *)(a | 0x80000000u);
    __asm__ __volatile__("ocbi @%0" : : "r"(p) : "memory");
    WATCH->ocw_idx[0] = ((a | 0x80000000u) >> 5) & 511u; WATCH->n_ocw = 1;
    const uint32_t n0 = neww(j, 0), n1 = neww(j, 1), n2 = neww(j, 2), n3 = neww(j, 3), n4 = neww(j, 4), n5 = neww(j, 5),
                   n6 = neww(j, 6), n7 = neww(j, 7);
    (void)R32(P2A(a));
    t0 = wdt_sync(RST_MODE);
    while ((t = pmc() - t0) < tgt) ;
    p[0] = n0; p[1] = n1; p[2] = n2; p[3] = n3; p[4] = n4; p[5] = n5; p[6] = n6; p[7] = n7;
    __asm__ __volatile__("ocbwb @%0" : : "r"(p) : "memory");
    break;
  }
  case K_DMA:
    a = dma_src(); b = dma_dst(j);
    for (int i = 0; i < 64; i++) { R32(P2A(a) + 4u * (uint32_t)i) = neww(j, i); R32(P2A(b) + 4u * (uint32_t)i) = oldw(j, i); }
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
    R32(SAR(1)) = a; R32(DAR(1)) = b; R32(DMATCR(1)) = 8;
    (void)R32(DMATCR(1));
    t0 = wdt_sync(RST_MODE);
    while ((t = pmc() - t0) < tgt) ;
    R32(CHCR(1)) = 0x00005441u;                            /* DM SM increment, auto request, 32-byte units, DE */
    break;
  case K_G2W:
    a = g2_reg(j);
    R32(a) = 0x1000u + (uint32_t)j;
    (void)R32(a);                                          /* a G2 read waits for Holly's FIFO to drain */
    t0 = wdt_sync(RST_MODE);
    while ((t = pmc() - t0) < tgt) ;
    R32(a) = 0x2000u + (uint32_t)j;
    break;
  case K_FMOV:
    a = P2A(fm_pa(j));
    R32(a) = oldw(j, 0); R32(a + 4) = oldw(j, 1);
    (void)R32(a);
    t0 = wdt_sync(RST_MODE);
    while ((t = pmc() - t0) < tgt) ;
    st64(a, neww(j, 0), neww(j, 1));
    break;
  default:                                                 /* the calibration: the timestamp loop alone */
    t0 = wdt_sync(RST_MODE);
    t = 0;
    break;
  }
  PER[P_START] = t;
  ts_loop(t0);
}

int main(void) {
  bm_default = h_any;
  imask(15);
  if (CAP->boots == 1 || PER[P_MAGIC] != MAGIC) {
    PER[P_MAGIC] = MAGIC; PER[P_FAILS] = 0; PER[P_NSC] = (uint32_t)nsc();
    /* the interval-mode overflow after the same synchronisation (for the report) */
    uint32_t t0 = wdt_sync(0);
    while (!(R8(WTCSR) & W_IOVF)) ;
    PER[P_TIOVF] = pmc() - t0;
    wdt_stop();
    printf("RFLY start scenarios=%lu tiovf=%lu\n", (unsigned long)PER[P_NSC], (unsigned long)PER[P_TIOVF]);
    PER[P_TREF] = PER[P_TIOVF];
    run(0);
  }
  const int j = (int)PER[P_J];
  if (!check_prev(j, (int)PER[P_KIND], (int)PER[P_LEAD])) PER[P_FAILS] = PER[P_FAILS] + 1;
  if (j == 0) PER[P_TREF] = PER[P_TS];                     /* the measured reset time (last timestamp) */
  if (j < (int)PER[P_NSC]) run(j + 1);
  printf("RFLY done scenarios=%lu fails=%lu\n", (unsigned long)PER[P_NSC], (unsigned long)PER[P_FAILS]);
  PER[P_MAGIC] = 0;
  return 0;
}
