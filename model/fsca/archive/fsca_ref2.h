/* ARCHIVED 2026-09-18: working reference of the structural model (made bit-exact in its last revision).  Superseded by
 * ../sh4_fsca.h, which is the same arithmetic in portable C99 with the hardware's ROM organisation and the measured FPSCR
 * behaviour.  Kept only so the historical tools in ../tools still build. */
/* SH7091 FSCA bit-exact value model, 2026-09-18.
 * Verified on all 65536 angles, sine/cosine, RN/RZ: zero mismatches.
 * Odd terms and C0 are unchanged. The even term REUSES the complementary
 * linear field; there is no independent sine field in the exact datapath:
 *   dx = t * 8192; K = 2^19; KC = 0x648340 = 102925 << 6
 *   I1 = (arr(KC, dx) + K) >> 23
 *   J  = (arr(dx, I1) + K) >> 23
 *   T2 = sm(arr(C1F[16-m], J) + K, 27-E1[16-m])
 * Every feedback latch is the product register >>> 3, as in FSRRA.
 * KC is a recovered global constant (~economised 4*pi at scale 2^19).
 * Its historical generation rule is not established. C0 is recovered from
 * centre samples; C1F and C3 retain their derived rules. No per-entry fixes.
 * FSCA2_S/U2 below are retained only for compatibility with historical tools;
 * they are NOT used by the exact model. See tools/rt56_exact.cpp and NOTES.md.
 */
#ifndef FSCA_REF2_H
#define FSCA_REF2_H
#include <stdint.h>
static const int64_t FSCA2_C1F[17] = {0x6487ed,0x640c00,0x62996b,0x6033bf,0x5ce0e5,0x58a910,0x5396a4,0x4db624,0x47160c,0x7f8d66,0x6fb43b,0x5ec7aa,0x4cf16d,0x74bafb,0x4e7352,0x4ed47f,0x0};
static const int      FSCA2_E1[17] = {0,0,0,0,0,0,0,0,0,-1,-1,-1,-1,-2,-2,-3,0};
static const int64_t  FSCA2_C3[17] = {0xa56,0xa49,0xa23,0x9e4,0x98c,0x91d,0x898,0x7fd,0x74f,0x68f,0x5be,0x4df,0x3f5,0x300,0x204,0x103,0x0};
static const int64_t   FSCA2_S[17] = {0x0,0x645e9b,0x63e2e1,0x4a5019,0x61f78b,0x78ad75,0x471ced,0x5133cd,0x5a827a,0x62f202,0x6a6d99,0x70e2cc,0x7641af,0x7a7d05,0x7d8a5f,0x7f6237,0x7fffff};
static const int      FSCA2_U2[17] = {30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static const int64_t  FSCA2_C0[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
#define FSCA2_KC 6587200LL
#define FSCA2_K  (1LL<<19)
static int64_t fsca2_arr(int64_t rec, int64_t mul) { int64_t R[14], T = 0; int k;
    for (k = 0; k < 14; k++) { int b1 = 2*k+1 > 62 ? 62 : 2*k+1, b0 = 2*k > 62 ? 62 : 2*k, bm = 2*k-1;
        int d = -2*(int)((rec >> b1) & 1) + (int)((rec >> b0) & 1) + (bm < 0 ? 0 : (int)((rec >> bm) & 1));
        int64_t mag = (int64_t)(d < 0 ? -d : d) * mul, row = d < 0 ? ~mag : mag;
        R[k] = row * (1LL << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1LL << (2*k); }
    { int m15 = (int)((R[0]>>15)&1) + (int)((R[1]>>15)&1) + (int)((R[2]>>15)&1);
      int p16 = (int)(((R[6]>>16) ^ (R[7]>>16) ^ (R[8]>>16)) & 1);
      T += (1LL<<16) * (m15 >= 2); T -= (1LL<<16) * p16; } return T; }
static int64_t fsca2_sm(int64_t P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static int64_t fsca2_internal(uint32_t angle16) {
    int q = (int)((angle16 + 512u) >> 10), m = q & 63, t = (int)angle16 - 1024*q, neg = 0, flip = 0;
    int64_t dx, T1, A3, T3, I1, J, T2, s, V;
    if (t < -512) t += 65536;
    if (m >= 33) { m -= 32; neg = 1; } if (m >= 17) { m = 32 - m; flip = 1; }
    dx = (int64_t)t << 13; s = flip ? -1 : 1;
    T1 = FSCA2_C1F[m] ? fsca2_sm(fsca2_arr(FSCA2_C1F[m], dx) + FSCA2_K, 23 - FSCA2_E1[m]) : 0;
    A3 = ((fsca2_arr(((int64_t)t*t) << 3, dx) + FSCA2_K) >> 20) >> 3;
    T3 = FSCA2_C3[m] ? fsca2_sm(fsca2_arr(FSCA2_C3[m] << 5, A3) + FSCA2_K, 26) : 0;
    I1 = (fsca2_arr(FSCA2_KC, dx) + FSCA2_K) >> 23; J = (fsca2_arr(dx, I1) + FSCA2_K) >> 23;
    T2 = FSCA2_C1F[16-m] ? fsca2_sm(fsca2_arr(FSCA2_C1F[16-m], J) + FSCA2_K, 27-FSCA2_E1[16-m]) : 0;
    V = FSCA2_C0[m] + s*T1 - T2 - s*T3; return neg ? -V : V; }
static uint32_t fsca2_to_float(int64_t V, int rz) { uint32_t sign = V < 0; uint64_t m = (uint64_t)(V<0?-V:V), q, r, h;
    int p, sh; if (!m) return sign << 31; p = 63 - __builtin_clzll(m); sh = p - 23;
    if (sh <= 0) q = m << (-sh); else { r = m & ((1ULL<<sh)-1); h = 1ULL << (sh-1); q = m >> sh;
        if (!rz) q += (r > h) || (r == h && (q & 1)); if (q >> 24) { q >>= 1; p++; } }
    return (sign << 31) | ((uint32_t)(127 - 26 + p) << 23) | ((uint32_t)q & 0x7fffff); }
static uint32_t fsca2_sin(uint32_t fpul, int rz) { return fsca2_to_float(fsca2_internal(fpul & 0xffffu), rz); }
static uint32_t fsca2_cos(uint32_t fpul, int rz) { return fsca2_to_float(fsca2_internal((fpul + 0x4000u) & 0xffffu), rz); }
#endif
