/* mix.h -- the M3 verification's random memory-path mix (tb/bm/soc-adv/m3, from agents/verify/M3/adv/src; coh.c's, shared with dmax.c and
 * irqx.c).  A working set of MIX_NL lines at MIX_BASE, 16 OC indices (MIX_STRIDE apart) x MIX_NL / 16 aliases
 * (MIX_ALIAS apart); every operation keeps it coherent from the CPU's point of view, so the shadow `sh` predicts every
 * load (see coh.c's head).  The including file defines MIX_TAG (the output prefix). */
#pragma once
#include "m3.h"
#ifndef MIX_BASE
#define MIX_BASE TR_PA
#endif
#ifndef MIX_ALIAS
#define MIX_ALIAS 0x4000u
#endif
#ifndef MIX_NIDX
#define MIX_NIDX 16
#endif
#ifndef MIX_SEED
#define MIX_SEED 0x1234567u
#endif
#ifndef MIX_STRIDE
#define MIX_STRIDE 0x200u
#endif
#ifndef NOPS
#define NOPS 6000
#endif
#ifndef NL
#define NL 64
#endif
static uint32_t sh[NL][8] __attribute__((aligned(32)));
static uint32_t src64[2] __attribute__((aligned(8)));
static uint32_t dst64[2] __attribute__((aligned(8)));
static uint32_t errors, nloads, nops;
static uint32_t seed = MIX_SEED;
static int wt_p0;                                       /* CCR.WT: P0 / P3 write-through */

static inline uint32_t lpa(int i) {
  return MIX_BASE + (uint32_t)(i / MIX_NIDX) * MIX_ALIAS + (uint32_t)(i % MIX_NIDX) * MIX_STRIDE;
}

extern void sq_line(uint32_t sqaddr, const uint32_t *w);      /* mix_asm: 8 SQ stores, PREF */

static inline void cp64(uint32_t dst, uint32_t src) {
  __asm__ __volatile__("fschg\n\tfmov @%1,dr0\n\tfmov dr0,@%0\n\tfschg" : : "r"(dst), "r"(src) : "memory", "fr0", "fr1");
}
static inline uint8_t shb(int i, uint32_t o) { return (uint8_t)(sh[i][o >> 2] >> (8 * (o & 3))); }
static inline void shb_set(int i, uint32_t o, uint8_t v) {
  uint32_t s = 8 * (o & 3);
  sh[i][o >> 2] = (sh[i][o >> 2] & ~(0xFFu << s)) | ((uint32_t)v << s);
}
static uint32_t sh_get(int i, uint32_t o, int n) {
  uint32_t v = 0;
  for (int k = 0; k < n; k++) v |= (uint32_t)shb(i, o + (uint32_t)k) << (8 * k);
  return v;
}
static void sh_put(int i, uint32_t o, int n, uint32_t v) {
  for (int k = 0; k < n; k++) shb_set(i, o + (uint32_t)k, (uint8_t)(v >> (8 * k)));
}
static void bad(const char *what, int i, uint32_t o, uint32_t got, uint32_t want) {
  errors++;
  if (errors <= 20)
    printf(MIX_TAG " MISMATCH op=%lu %s line=%d pa=%08lx off=%lu got=%08lx want=%08lx\n", (unsigned long)nops, what, i,
           (unsigned long)lpa(i), (unsigned long)o, (unsigned long)got, (unsigned long)want);
}
static uint32_t ld(uint32_t a, int n) {
  return n == 1 ? R8(a) : n == 2 ? R16(a) : R32(a);
}
static void st(uint32_t a, int n, uint32_t v) {
  if (n == 1) R8(a) = (uint8_t)v; else if (n == 2) R16(a) = (uint16_t)v; else R32(a) = v;
}

/* one random operation */
static void op(int phase) {
  uint32_t r = lcg(&seed);
  int i = (int)(r % NL);
  uint32_t k = (r >> 6) % 100u;
  uint32_t v = lcg(&seed) ^ (nops * 0x9E3779B9u);
  int n = 1 << ((r >> 13) & 3);                        /* 1 2 4 8 */
  uint32_t o = ((r >> 16) & 31u) & ~(uint32_t)(n - 1);
  uint32_t pa = lpa(i);
  nops++;
  if (k < 22) {                                        /* load: P1, P0 or P3 */
    uint32_t va = ((r >> 21) & 3) == 0 ? P0(pa) : ((r >> 21) & 3) == 1 ? P3(pa) : P1(pa);
    nloads++;
    if (n == 8) {
      cp64(P1((uint32_t)(uintptr_t)dst64), va + o);
      if (dst64[0] != sh_get(i, o, 4)) bad("ld64lo", i, o, dst64[0], sh_get(i, o, 4));
      if (dst64[1] != sh_get(i, o + 4, 4)) bad("ld64hi", i, o + 4, dst64[1], sh_get(i, o + 4, 4));
    } else {
      uint32_t g = ld(va + o, n);
      if (g != sh_get(i, o, n)) bad("ld", i, o, g, sh_get(i, o, n));
    }
  } else if (k < 44) {                                 /* store: P1, P0 or P3 */
    uint32_t va = ((r >> 21) & 3) == 0 ? P0(pa) : ((r >> 21) & 3) == 1 ? P3(pa) : P1(pa);
    if (n == 8) {
      src64[0] = v; src64[1] = ~v;
      cp64(va + o, P1((uint32_t)(uintptr_t)src64));
      sh_put(i, o, 4, v); sh_put(i, o + 4, 4, ~v);
    } else {
      st(va + o, n, v);
      sh_put(i, o, n, v);
    }
  } else if (k < 52) {                                 /* P2 after an OCBP of the line */
    ocbp(P1(pa));
    if ((r >> 21) & 1) {
      if (n == 8) {
        src64[0] = v; src64[1] = v * 3u;
        cp64(P2(pa) + o, P1((uint32_t)(uintptr_t)src64));
        sh_put(i, o, 4, v); sh_put(i, o + 4, 4, v * 3u);
      } else { st(P2(pa) + o, n, v); sh_put(i, o, n, v); }
    } else {
      nloads++;
      if (n == 8) {
        cp64(P1((uint32_t)(uintptr_t)dst64), P2(pa) + o);
        if (dst64[0] != sh_get(i, o, 4)) bad("p2ld64lo", i, o, dst64[0], sh_get(i, o, 4));
        if (dst64[1] != sh_get(i, o + 4, 4)) bad("p2ld64hi", i, o + 4, dst64[1], sh_get(i, o + 4, 4));
      } else {
        uint32_t g = ld(P2(pa) + o, n);
        if (g != sh_get(i, o, n)) bad("p2ld", i, o, g, sh_get(i, o, n));
      }
    }
  } else if (k < 57) {
    ocbwb(P1(pa));
  } else if (k < 62) {
    ocbp(P1(pa));
  } else if (k < 67) {
    pref(P1(pa));
  } else if (k < 71) {
    ocbwb(P1(pa)); ocbi(P1(pa));
  } else if (k < 75) {                                 /* MOVCA.L, then the rest of the line */
    movca(P1(pa), v);
    sh[i][0] = v;
    for (int w = 1; w < 8; w++) { R32(P1(pa) + 4u * (uint32_t)w) = v + (uint32_t)w; sh[i][w] = v + (uint32_t)w; }
  } else if (k < 79) {                                 /* TAS.B */
    uint32_t b = (r >> 16) & 31u;
    uint32_t t = tas(P1(pa) + b);
    if (t != (shb(i, b) == 0)) bad("tas", i, b, t, shb(i, b) == 0);
    shb_set(i, b, shb(i, b) | 0x80u);
  } else if (k < 85) {                                 /* store-queue burst over the whole line */
    uint32_t w[8];
    ocbp(P1(pa));
    for (int x = 0; x < 8; x++) { w[x] = v ^ ((uint32_t)x * 0x01010101u); sh[i][x] = w[x]; }
    sq_line(0xE0000000u | (pa & 0x03FFFFE0u), w);
  } else if (k < 93) {                                 /* a burst on one line: store, load, store, load */
    uint32_t a = P1(pa), a2 = ((r >> 21) & 1) ? P0(pa) : P1(pa);
    st(a + o, n > 4 ? 4 : n, v); sh_put(i, o, n > 4 ? 4 : n, v);
    uint32_t o2 = (o + 8u) & 31u & ~3u;
    nloads++;
    uint32_t g = R32(a2 + o2);
    if (g != sh[i][o2 >> 2]) bad("burst-ld", i, o2, g, sh[i][o2 >> 2]);
    R32(a2 + ((o2 + 4u) & 31u)) = v + 1u; sh[i][((o2 + 4u) & 31u) >> 2] = v + 1u;
    nloads++;
    g = R32(a + (o & ~3u));
    if (g != sh[i][o >> 2]) bad("burst-ld2", i, o & ~3u, g, sh[i][o >> 2]);
  } else {                                             /* two aliases of one index back to back: evict / refill */
    int i2 = (i + MIX_NIDX * (1 + (int)((r >> 21) % (uint32_t)(NL / MIX_NIDX - 1)))) % NL;
    R32(P1(pa) + (o & ~3u)) = v; sh[i][o >> 2] = v;
    nloads++;
    uint32_t g = R32(P1(lpa(i2)) + (o & ~3u));
    if (g != sh[i2][o >> 2]) bad("alias-ld", i2, o & ~3u, g, sh[i2][o >> 2]);
    nloads++;
    g = R32(P1(pa) + (o & ~3u));                       /* the victim just written back: refetched */
    if (g != v) bad("alias-back", i, o & ~3u, g, v);
    nloads++;
    g = R32(P2(pa) + ((o + 4u) & 28u));                /* a P2 read of a line that is cached clean (after the refetch) */
    if (sh_get(i, (o + 4u) & 28u, 4) != g) bad("alias-p2", i, (o + 4u) & 28u, g, sh_get(i, (o + 4u) & 28u, 4));
  }
  (void)phase; (void)wt_p0;
}

static void final_check(const char *tag) {
  uint32_t sum = 0, e0 = errors;
  for (int i = 0; i < NL; i++) ocbp(P1(lpa(i)));
  for (int i = 0; i < NL; i++)
    for (int w = 0; w < 8; w++) {
      uint32_t g = R32(P2(lpa(i)) + 4u * (uint32_t)w);
      sum = sum * 31u + g;
      if (g != sh[i][w]) bad(tag, i, 4u * (uint32_t)w, g, sh[i][w]);
    }
  printf(MIX_TAG " %s errors=%lu sum=%08lx\n", tag, (unsigned long)(errors - e0), (unsigned long)sum);
}

static void init_lines(void) {
  for (int i = 0; i < NL; i++) {
    ocbp(P1(lpa(i)));
    for (int w = 0; w < 8; w++) { uint32_t v = 0xC0000000u | ((uint32_t)i << 8) | (uint32_t)w; R32(P2(lpa(i)) + 4u * (uint32_t)w) = v; sh[i][w] = v; }
  }
}

