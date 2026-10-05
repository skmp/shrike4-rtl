/* dmax.c -- M3 verification: the DMAC on the real BSC while the CPU's whole memory path runs (rm).  Random dual-
 * address memory-to-memory transfers on channels 1, 2 and 3 (auto request, RS = 4, as the console's dmac1 probe uses on
 * the DC's DMAOR 0x8201), every unit size (1 2 4 8 32 bytes), source / destination fixed / increment / decrement,
 * cycle steal and burst, one to three channels at once, some ending with a DMTE interrupt (IE), some waited for in
 * SLEEP -- while the CPU runs coh.c's coherent random mix (mix.h) over its own region (misses, dirty write-backs, SQ
 * bursts, TAS.B's locked pair, P2 quadwords) and reads the destinations uncached while they are written.
 * Oracles: each transfer's destination (and the bytes around it) against a software replay of the transfer (hm 14:
 * units in order, SAR / DAR stepped per unit; the console's dmac1 S5 shows the fixed / decrement semantics); the final
 * SAR / DAR / DMATCR / CHCR; the CPU mix's shadow; --lockstep for every CPU access (the DMA writes reach Sh4Sys as DMAW
 * records at the RTL's times).
 * CASE (dmax_c<HEX>): bits 15..0 the seed; bit 16: the CPU mix off (the DMAC alone); bit 17: burst mode only; bit 18:
 * 40 transfers instead of 160; bit 19: a refresh every 48 Bcyc (RTCOR 3).
 * Output: "DMAX x=<n> ch=<c> ts=<t> n=<units> sm=<m> dm=<m> tm=<b> ie=<b> ok=<0|1>" for the first transfers and every
 * failing one, "DMAX summary xfers=<n> bad=<n> mix_errors=<n> dmte=<n>", "DMAX done". */
#define MIX_TAG "DMAX"
#ifdef CASE
#define MIX_SEED (0x7654321u ^ ((uint32_t)(CASE) * 0x9E3779B1u))
#endif
#include "mix.h"

#if defined(CASE) && ((CASE) & 0x40000)          /* dmax_c4xxxx: 40 transfers (the planted-bug runs) */
#define NX 40
#endif
#ifndef NX
#define NX 160
#endif
#define S_PA   (DR_PA)                         /* 64 KB of source data */
#define D_PA(c) (DR_PA + 0x20000u + (uint32_t)(c) * 0x10000u)   /* 64 KB destination window per channel */
#define WIN    0x10000u
static const uint32_t tsz[5] = {8, 1, 2, 4, 32};
static uint8_t expd[4][4096 + 256] __attribute__((aligned(32)));   /* expected destination bytes (window slice) */
static uint32_t xseed = 0xBADC0DEu;

typedef struct { int ch, ts, sm, dm, tm, ie; uint32_t n, sar, dar, lo, hi; } xf_t;

static uint8_t src_byte(uint32_t pa) { return (uint8_t)((pa * 0x2Du) ^ (pa >> 7) ^ 0x5Au); }

static void fill_src(void) {
  for (uint32_t a = 0; a < WIN; a += 4) {
    uint32_t pa = S_PA + a;
    R32(P2(pa)) = (uint32_t)src_byte(pa) | ((uint32_t)src_byte(pa + 1) << 8) | ((uint32_t)src_byte(pa + 2) << 16) |
                  ((uint32_t)src_byte(pa + 3) << 24);
  }
}

/* a random transfer: [lo, hi) is the destination span it may touch (plus 64 bytes of margin each side) */
static void pick(xf_t *x, int ch, int burst_only) {
  uint32_t r = lcg(&xseed);
  x->ch = ch;
  x->ts = (int)(r % 5u);
  uint32_t u = tsz[x->ts];
  x->sm = (int)((r >> 4) % 3u);
  x->dm = (int)((r >> 6) % 3u);
  if (x->ts == 4) { if (x->sm == 2) x->sm = 1; if (x->dm == 2) x->dm = 1; }   /* no 32-byte decrement */
  x->tm = burst_only ? 1 : (int)((r >> 8) & 1);
  x->ie = (int)((r >> 9) & 1);
  uint32_t maxn = 2048u / u; if (maxn > 96) maxn = 96;
  x->n = 1 + (lcg(&xseed) % maxn);
  uint32_t span = x->n * u;
  uint32_t so = (lcg(&xseed) % ((WIN - 2 * span - 64) / u)) * u + span;
  uint32_t dof = 256u + (lcg(&xseed) % ((4096u - span) / u + 1)) * u;   /* within the 4 KB slice at +256 */
  if (x->dm == 2) dof += span - u;                                       /* decrementing: the span below it */
  x->sar = S_PA + (so & (WIN - 1));
  x->dar = D_PA(ch) + dof;
  x->lo = D_PA(ch) + 256u - 64u;
  x->hi = D_PA(ch) + 256u + 4096u + 64u;
}

static uint32_t chcr_of(const xf_t *x) {
  return CH_DM(x->dm) | CH_SM(x->sm) | CH_RS(4) | (x->tm ? CH_TM : 0) | CH_TS(x->ts) | (x->ie ? CH_IE : 0);
}

/* the software replay: expd[ch] = the slice [D_PA + 192, D_PA + 192 + 4096 + 128) after the transfer */
static void prepare(const xf_t *x) {
  uint8_t *e = expd[x->ch];
  for (uint32_t a = x->lo; a < x->hi; a += 4) {
    uint32_t v = 0xEE000000u | (a & 0xFFFFFFu);
    R32(P2(a)) = v;
    for (int k = 0; k < 4; k++) e[a - x->lo + (uint32_t)k] = (uint8_t)(v >> (8 * k));
  }
  uint32_t u = tsz[x->ts], s = x->sar, d = x->dar;
  for (uint32_t i = 0; i < x->n; i++) {
    for (uint32_t k = 0; k < u; k++) e[d + k - x->lo] = src_byte(s + k);
    s = x->sm == 1 ? s + u : x->sm == 2 ? s - u : s;
    d = x->dm == 1 ? d + u : x->dm == 2 ? d - u : d;
  }
}

static void start(const xf_t *x) {
  (void)R32(CHCR(x->ch));
  R32(CHCR(x->ch)) = 0;
  R32(SAR(x->ch)) = x->sar; R32(DAR(x->ch)) = x->dar; R32(DMATCR(x->ch)) = x->n;
  R32(CHCR(x->ch)) = chcr_of(x) | CH_DE;
}

static int finish(const xf_t *x, int idx, int verbose) {
  uint32_t u = tsz[x->ts];
  uint32_t esar = x->sm == 1 ? x->sar + x->n * u : x->sm == 2 ? x->sar - x->n * u : x->sar;
  uint32_t edar = x->dm == 1 ? x->dar + x->n * u : x->dm == 2 ? x->dar - x->n * u : x->dar;
  uint32_t c = R32(CHCR(x->ch)), s = R32(SAR(x->ch)), d = R32(DAR(x->ch)), t = R32(DMATCR(x->ch));
  int ok = t == 0 && (c & CH_TE) && s == esar && d == edar;
  uint32_t nbad = 0, first = 0;
  const uint8_t *e = expd[x->ch];
  for (uint32_t a = x->lo; a < x->hi; a += 4) {
    uint32_t g = R32(P2(a)), w = (uint32_t)e[a - x->lo] | ((uint32_t)e[a - x->lo + 1] << 8) |
                 ((uint32_t)e[a - x->lo + 2] << 16) | ((uint32_t)e[a - x->lo + 3] << 24);
    if (g != w) { if (!nbad) first = a; nbad++; }
  }
  if (nbad) ok = 0;
  if (!ok || verbose)
    printf("DMAX x=%d ch=%d ts=%d n=%lu sm=%d dm=%d tm=%d ie=%d sar=%08lx dar=%08lx -> chcr=%08lx sar=%s dar=%s "
           "tcr=%lu words_bad=%lu first=%08lx ok=%d\n", idx, x->ch, x->ts, (unsigned long)x->n, x->sm, x->dm, x->tm,
           x->ie, (unsigned long)x->sar, (unsigned long)x->dar, (unsigned long)c, s == esar ? "ok" : "BAD",
           d == edar ? "ok" : "BAD", (unsigned long)t, (unsigned long)nbad, (unsigned long)first, ok);
  (void)R32(CHCR(x->ch));
  R32(CHCR(x->ch)) = 0;
  return ok;
}

int main(void) {
#ifdef CASE
  const int mix_on = !((CASE) & 0x10000), burst_only = ((CASE) & 0x20000) != 0;
  xseed ^= (uint32_t)(CASE) * 0x85EBCA6Bu;
#else
  const int mix_on = 1, burst_only = 0;
#endif
  printf("DMAX start nx=%d mix=%d burst_only=%d dmaor=%08lx\n", NX, mix_on, burst_only, (unsigned long)R32(DMAOR));
#if defined(CASE) && ((CASE) & 0x80000)
  R16(RTCOR) = 0xA503;                              /* dmax_c8xxxx: a refresh every 48 Bcyc */
#endif
  R32(QACR0) = 0x0C; R32(QACR1) = 0x0C;
  init_lines();
  fill_src();
  /* a slow TMU0 as the SLEEP backstop (a DMTE that came before the SLEEP) */
  tmu_reset();
  R16(IPRA) = 0x2000;                               /* TMU0 level 2 */
  R16(IPRC) = 0x0000;
  R32(TCOR0) = 4000; R32(TCNT0) = 4000; R16(TCR0) = UNIE;
  R8(TSTR) = 1;
  g_irq_quiet = 1;
  /* DMTE at level 5 (IPRC[11:8]: DMAC, hm Table 19.5) */
  R16(IPRC) = 0x0500;
  int bad = 0, nxf = 0, nsleep = 0;
  for (int it = 0; it < NX; ) {
    int nch = 1 + (int)(lcg(&xseed) % 3u);
    xf_t xs[3];
    int chs[3] = {1, 3, 2};
    for (int k = 0; k < nch; k++) { pick(&xs[k], chs[k], burst_only); prepare(&xs[k]); }
    uint32_t dmte0[4];
    for (int c = 0; c < 4; c++) dmte0[c] = g_ndmte[c];
    imask(0);
    for (int k = 0; k < nch; k++) start(&xs[k]);
    int use_sleep = (lcg(&xseed) & 3u) == 0;
    uint32_t t0 = pmc();
    for (;;) {
      int busy = 0;
      for (int k = 0; k < nch; k++) if (R32(DMATCR(xs[k].ch)) != 0 || !(R32(CHCR(xs[k].ch)) & CH_TE)) busy = 1;
      if (!busy) break;
      if (pmc() - t0 > 3000000u) { printf("DMAX x=%d TIMEOUT\n", it); bad++; break; }
      if (use_sleep) { __asm__ __volatile__("sleep"); nsleep++; continue; }
      if (mix_on) op(0);
      /* an uncached read of a destination while it is written (lockstep: the DMAW order) */
      uint32_t k = lcg(&xseed) % (uint32_t)nch;
      (void)R32(P2(xs[k].dar & ~3u));
    }
    imask(15);
    for (int k = 0; k < nch; k++) {
      if (!finish(&xs[k], it + k, it < 12)) bad++;
      if (xs[k].ie && g_ndmte[xs[k].ch] == dmte0[xs[k].ch]) { printf("DMAX x=%d ch=%d: IE set, no DMTE taken\n", it + k, xs[k].ch); bad++; }
    }
    it += nch; nxf += nch;
  }
  R8(TSTR) = 0; tmu_reset();
  final_check("mixfinal");
  printf("DMAX summary xfers=%d bad=%d mix_errors=%lu dmte=%lu/%lu/%lu sleeps=%d dmaor=%08lx\n", nxf, bad,
         (unsigned long)errors, (unsigned long)g_ndmte[1], (unsigned long)g_ndmte[2], (unsigned long)g_ndmte[3], nsleep,
         (unsigned long)R32(DMAOR));
  printf("DMAX done\n");
  return (bad || errors) ? 1 : 0;
}
