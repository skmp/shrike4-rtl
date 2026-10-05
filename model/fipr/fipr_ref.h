/* NOTE: the shareable reference is sh4_fipr.h (same values; adds FPSCR.DN = 0, FTRV, the measured FPSCR / trap behaviour and
 * portable C).  This file stays as the compact DN = 1 value model used by the older tools and the RTL testbench.
 * SH7091 FIPR reference model, integer-only C99. Verified bit-exact against 32M + 5.2M hardware vectors (RN and RZ).
 * fipr_ref(a, b, rz): a = FVm (first source), b = FVn (second source, the Booth-recoded operand); rz = 1 for FPSCR.RM = RZ.
 * Assumes FPSCR.DN = 1 (denormals flushed) and PR = 0. */
#ifndef FIPR_REF_H
#define FIPR_REF_H
#include <stdint.h>
static inline int fipr_booth_digit(uint32_t x, int k) { int b_1 = k ? (int)((x >> (2*k-1)) & 1) : 0, b0 = (int)((x >> (2*k)) & 1), b1 = (int)((x >> (2*k+1)) & 1); return -2*b1 + b0 + b_1; }
/* 24x24 -> 28-bit product: rows |d|*mul (25 bits, one's complement for d<0) at 2k, truncated below column 16; hot ones only from
 * column 18; sign extension exact; group (rows 0,1,2) keeps its column-15 carry; group (rows 6,7,8) loses its column-16 sum bit; +2^19; >>20 */
static inline int64_t fipr_mult28(uint32_t rec, uint32_t mul) {
    uint64_t R[13]; int64_t T = 0; int k;
    for (k = 0; k < 13; k++) {
        int d = fipr_booth_digit(rec, k); uint32_t ad = (uint32_t)(d < 0 ? -d : d); uint64_t mag = (uint64_t)ad * mul;
        uint64_t oc = d < 0 ? (~mag & ((1ull << 25) - 1)) : mag; R[k] = oc << (2*k);
        T += (int64_t)((R[k] >> 16) << 16);
        if (d < 0) { if (2*k >= 18) T += (int64_t)1 << (2*k); T -= (int64_t)1 << (25 + 2*k); }
    }
    { int m15 = (int)(((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1)); if (m15 >= 2) T += (int64_t)1 << 16;
      int p16 = (int)(((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1); if (p16) T -= (int64_t)1 << 16; }
    return (T + ((int64_t)1 << 19)) >> 20;
}
static inline uint32_t fipr_ref(const uint32_t a[4], const uint32_t b[4], int rz) {
    int nan = 0, pinf = 0, ninf = 0, n = 0, i; int sign0 = (int)(((a[0] ^ b[0]) >> 31) & 1);
    int ps[4], pE[4]; int64_t pp[4];
    for (i = 0; i < 4; i++) {
        int ea = (int)((a[i] >> 23) & 0xff), eb = (int)((b[i] >> 23) & 0xff); uint32_t fa = a[i] & 0x7fffff, fb = b[i] & 0x7fffff; int sa = (int)(a[i] >> 31), sb = (int)(b[i] >> 31);
        int za = (ea == 0), zb = (eb == 0);
        int ia = (ea == 255 && fa == 0), ib = (eb == 255 && fb == 0), na = (ea == 255 && fa != 0), nb = (eb == 255 && fb != 0);
        if (na || nb) { nan = 1; continue; }
        if ((ia && zb) || (ib && za)) { nan = 1; continue; }
        if (ia || ib) { if (sa ^ sb) ninf++; else pinf++; continue; }
        if (za || zb) continue;
        if (ea + eb <= 75) continue;
        { int64_t p = fipr_mult28(fb | 0x800000u, fa | 0x800000u); if (!p) continue; ps[n] = (sa ^ sb) ? -1 : 1; pE[n] = ea + eb; pp[n] = p; n++; }
    }
    if (nan || (pinf && ninf)) return 0x7fbfffffu;
    if (pinf + ninf >= 2) return sign0 ? 0xff800000u : 0x7f800000u;
    if (pinf) return 0x7f800000u; if (ninf) return 0xff800000u;
    if (!n) return 0;
    { int Emax = pE[0]; int64_t S = 0; uint32_t sign; int top, sh, e; int64_t q;
      for (i = 1; i < n; i++) if (pE[i] > Emax) Emax = pE[i];
      for (i = 0; i < n; i++) { int d = Emax - pE[i]; int64_t x = pp[i]; if (d > 0) x = d >= 63 ? 0 : (x >> d); S += ps[i] * x; }
      if (!S) return 0;
      sign = S < 0; if (S < 0) S = -S;
      top = 63 - __builtin_clzll((uint64_t)S); sh = top - 23;
      if (sh > 0) { int64_t r = S & (((int64_t)1 << sh) - 1), h = (int64_t)1 << (sh-1); q = S >> sh; if (!rz) q += (r > h) || (r == h && (q & 1)); }
      else q = S << (-sh);
      if (q >> 24) { q >>= 1; top++; }
      e = top + Emax - 280 + 127;
      if (e <= 0) return sign << 31;
      if (e >= 255) return (sign << 31) | (rz ? 0x7f7fffffu : 0x7f800000u);
      return (sign << 31) | ((uint32_t)(e & 0xff) << 23) | ((uint32_t)q & 0x7fffff); }
}
#endif
