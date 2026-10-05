// HISTORICAL 148-violation hypothesis. Preserved for reproducible research tools.
// The exact model is ../fsca_ref2.h; see rt56_exact.cpp (2026-09-18 breakthrough).
// base2.h -- clean re-derivation of the structural FSCA model (2026-09-18 session).
// Everything integer. Coefficient tables are DERIVED from the published rules, not fitted:
//   C1 = RN23(cos(n*pi/32) * pi/2 * 2^23)     (linear, 23-bit field + octave exponent)
//   c3 = RN(cos(n*pi/32) * 2646)              (cubic, 12-bit, weight 2^-54)
//   S  = RN(sin(n*pi/32) * 2^(23+k)) in [2^22,2^23), clamped to 2^23-1   (square's table sine)
//   u2 = 30 + k                                (k = leading-zero octaves of sin)
//   C0 = the hardware value at t = 0 (26-bit table field); candidate RN(sin*2^26) clamped.
#pragma once
#include "fsca_common.h"
#include <cmath>

// ---- FIPR Booth array, verbatim from pat_full3.cpp (rows cut below col 16, hot ones from col 18, two group rules)
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2);
    int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19;
static const i64 KC = 0x4ef145;                 // economised 2*pi^2 at 2^18

// ---- integer trig (Q60, __int128), same method as linear_precision.cpp
typedef __int128 i128;
static const int QQ = 60; static const i128 QONE = (i128)1 << QQ;
static i128 atan_inv_(int d) { i128 p = QONE / d, s = p; for (int k = 1; p != 0; ++k) { p /= (i128)d*d; if (k & 1) s -= p/(2*k+1); else s += p/(2*k+1); } return s; }
static i128 PI_() { static i128 v = 16*atan_inv_(5) - 4*atan_inv_(239); return v; }
static i128 cos_(i128 x) { i128 x2 = x*x/QONE, t = QONE, s = QONE; for (int k = 1; t != 0; ++k) { t = t*x2/QONE/((2*k-1)*(2*k)); if (k & 1) s -= t; else s += t; } return s; }
static i128 sin_(i128 x) { i128 x2 = x*x/QONE, t = x, s = x; for (int k = 1; t != 0; ++k) { t = t*x2/QONE/((2*k)*(2*k+1)); if (k & 1) s -= t; else s += t; } return s; }
static i64 rn_scale(i128 v, int sh) {   // RN(v * 2^sh) with v in Q60, v >= 0
    i128 num = sh >= 0 ? v << sh : v >> (-sh); i128 q = num / QONE, r = num % QONE;
    if (r*2 > QONE || (r*2 == QONE && (q & 1))) ++q; return (i64)q; }

struct Tab { i64 C1f; int e1; i64 c3; i64 S; int u2; i64 C0; i64 sin26, cos26; };
static Tab TB[17];
static void build_tables() {
    for (int n = 0; n <= 16; n++) {
        i128 a = PI_()*n/32; i128 c = cos_(a), s = sin_(a);
        // linear: RN23 of cos*pi/2*2^23 held as a 24-bit normalised mantissa (even) + octave
        i128 v = c*PI_()/QONE*((i128)1<<22);            // cos*pi*2^22 = cos*(pi/2)*2^23
        int e = 0; if (n != 16) { while (v < ((i128)1<<23)*QONE) { v <<= 1; --e; } }
        i128 q = v/(2*QONE), r = v%(2*QONE); if (r > QONE || (r == QONE && (q & 1))) ++q;
        TB[n].C1f = n == 16 ? 0 : (i64)q;               // the 23-bit recoded field (= mantissa>>1)
        TB[n].e1 = e;
        // cubic 12-bit: RN(cos*2646)
        i128 cu = c*PI_()/QONE; cu = cu*PI_()/QONE; cu = cu*PI_()/QONE; cu = cu*512/6;   // cos*pi^3*512/6 = cos*2646.06
        TB[n].c3 = n == 16 ? 0 : (i64)((cu + QONE/2)/QONE);
        // square's table sine: 23-bit normalised field + u2
        int k = 0; if (n > 0) { while (s*((i128)1<<(k+1)) < QONE) k++; }   // sin in [2^-(k+1), 2^-k)
        if (n == 0) { TB[n].S = 0; TB[n].u2 = 30; }
        else { i64 sf = rn_scale(s, 23+k); if (sf >= (1ll<<23)) { sf = (1ll<<23)-1; } TB[n].S = sf; TB[n].u2 = 30+k; }
        i64 c026 = rn_scale(s, 26); if (c026 >= (1ll<<26)) c026 = (1ll<<26)-1;
        TB[n].C0 = c026; TB[n].sin26 = c026; TB[n].cos26 = rn_scale(c, 26);
    }
}
// ---- the model: sine internal value at node n (0..16 first quadrant), offset t, using table TB
static inline i64 model_V(int n, int t, const Tab& T) {
    i64 dx = (i64)t << 13;
    i64 T1 = T.C1f ? sm(arr(T.C1f, dx) + K, 23 - T.e1) : 0;
    i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
    i64 T3 = T.c3 ? sm(arr(T.c3 << 5, A3) + K, 26) : 0;
    i64 I1 = (arr(KC, dx) + K) >> 23;
    i64 I2 = (arr(I1, dx) + K) >> 20;
    i64 T2 = T.S ? sm(arr(I2, T.S) + K, T.u2) : 0;
    return T.C0 + T1 - T2 - T3;
}
// square product (pre-shift) and its two chain latches, for diagnostics
static inline void sq_parts(int t, i64 S, i64& I1, i64& I2, i64& P) {
    i64 dx = (i64)t << 13; I1 = (arr(KC, dx) + K) >> 23; I2 = (arr(I1, dx) + K) >> 20; P = arr(I2, S) + K; }
// angle -> (node, t) with node folded into 0..16 plus sign/mirror info is done by callers.
static inline uint32_t to_float(i64 V, int rz) {
    uint32_t sign = V < 0; u64 m = (u64)(V < 0 ? -V : V); if (!m) return 0;
    int p = 63 - __builtin_clzll(m), sh = p - 23; u64 q;
    if (sh <= 0) q = m << (-sh); else { u64 r = m & ((1ull<<sh)-1), h = 1ull << (sh-1); q = m >> sh; if (!rz) q += (r > h) || (r == h && (q & 1)); if (q >> 24) { q >>= 1; p++; } }
    return (sign << 31) | ((uint32_t)(127 - 26 + p) << 23) | ((uint32_t)q & 0x7fffff); }
