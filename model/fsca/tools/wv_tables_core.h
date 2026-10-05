// wv_tables_core.h -- INDEPENDENT re-implementation of the FSCA structural model, written for the
// adversarial verification of the "square latch at column 18" claim.
//
// Nothing here is taken from wd_*.cpp / wd_core.h.  The two things that are transcribed from the
// ESTABLISHED record (CURRENT.md section 3, which predates the claim) are:
//   * the FIPR Booth array `arr` (rows truncated below col 16, hot ones from col 18, two group rules)
//   * the coefficient rules C1 = RN23(cos*pi/2*2^23) + octave, c3 = RN(cos*2646), S = RN(sin*2^(23+k))
// Both are cross-checked against base2.h in wv_tables_selfcheck.cpp.
//
// The square path is parameterised exactly as the claim states it:
//   dx = t<<13
//   I1 = (arr(KC, dx) + k1) >> s1            KC = 0x4EF145, s1 = 23, k1 = 2^19
//   I2 = (arr(I1, dx) + k2) >> s2            s2 in [14,24]      <-- the claim moves this from 20 to 18
//   T2 = sm(arr(I2, S_n) + K, u2_n)          u2_n = (50 - s2) + k_n   (the claimed scale law)
//   V  = C0_n + s*T1 - T2 - s*T3             s = -1 for nodes 17..31
#pragma once
#include "fsca_common.h"
#include <vector>
#include <algorithm>
#include <cstring>

// ------------------------------------------------------------------ the array (CURRENT.md section 3)
static i64 wv_arr(i64 rec, i64 mul) {
    i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) {
        auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); };
        int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul;
        i64 row = (d < 0) ? ~mag : mag;                 // one's complement for a negative digit
        R[k] = row * (1ll << (2*k));
        T += (R[k] >> 16) << 16;                        // every row truncated below column 16
        if (d < 0 && 2*k >= 18) T += 1ll << (2*k);      // hot one survives only from column 18
    }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1);
    T += (1ll << 16) * (m15 >= 2);                      // col-15 majority of rows 0,1,2
    int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1;
    T -= (1ll << 16) * p16;                             // col-16 parity of rows 6,7,8
    return T;
}
// how many Booth rows a recoded operand actually lights up (rows 0..12 are what a 24-bit FIPR operand uses)
static int wv_rows_used(i64 rec) {
    i64 m = rec < 0 ? ~rec : rec; int top = 0;
    for (int b = 62; b >= 0; --b) if ((m >> b) & 1) { top = b; break; }
    return top / 2 + 1;   // digit index top/2 is the highest that can be non-zero, +1 for count
}
static inline i64 wv_sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 WK  = 1ll << 19;
static const i64 WKC = 0x4ef145;

// ------------------------------------------------------------------ integer trig, Q60, my own code
typedef __int128 wi128;
static const int WQ = 60;
static const wi128 WONE = (wi128)1 << WQ;
static wi128 wv_atan_recip(int d) {          // atan(1/d) in Q60
    wi128 p = WONE / d, s = p;
    for (int k = 1; p != 0; ++k) { p /= (wi128)d * d; if (k & 1) s -= p / (2*k+1); else s += p / (2*k+1); }
    return s;
}
static wi128 wv_pi() { static wi128 v = 16 * wv_atan_recip(5) - 4 * wv_atan_recip(239); return v; }
static wi128 wv_cos(wi128 x) { wi128 x2 = x*x/WONE, t = WONE, s = WONE;
    for (int k = 1; t != 0; ++k) { t = t*x2/WONE/((2*k-1)*(2*k)); if (k & 1) s -= t; else s += t; } return s; }
static wi128 wv_sin(wi128 x) { wi128 x2 = x*x/WONE, t = x, s = x;
    for (int k = 1; t != 0; ++k) { t = t*x2/WONE/((2*k)*(2*k+1)); if (k & 1) s -= t; else s += t; } return s; }
static i64 wv_rn(wi128 v, int sh) {          // round-to-nearest-even of v*2^sh, v >= 0 in Q60
    wi128 num = sh >= 0 ? (v << sh) : (v >> (-sh));
    wi128 q = num / WONE, r = num % WONE;
    if (r*2 > WONE || (r*2 == WONE && (q & 1))) ++q;
    return (i64)q;
}

// ------------------------------------------------------------------ coefficient tables
struct WTab { i64 C1f; int e1; i64 c3; i64 S; int k; i64 C0; i64 rn26; };
static WTab WT[17];
// C0 as published by the claim (identical to the b2/b10/b12 list; independently re-checked
// against the t=0 windows in wv_tables_main.cpp).
static const i64 WV_C0[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,
                              47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static void wv_build(int sinw = 23) {
    for (int n = 0; n <= 16; n++) {
        wi128 a = wv_pi() * n / 32, c = wv_cos(a), s = wv_sin(a);
        // linear: |C1| = cos * pi/2 * 2^23 rounded to 23 significant bits, held as field + octave
        wi128 v = c * wv_pi() / WONE * ((wi128)1 << 22);       // cos * pi * 2^22
        int e = 0;
        if (n != 16) while (v < ((wi128)1 << 23) * WONE) { v <<= 1; --e; }
        wi128 q = v / (2*WONE), r = v % (2*WONE);
        if (r > WONE || (r == WONE && (q & 1))) ++q;
        WT[n].C1f = (n == 16) ? 0 : (i64)q;
        WT[n].e1  = e;
        // cubic: RN(cos * pi^3 * 512 / 6)  = RN(cos * 2646.06)
        wi128 cu = c * wv_pi() / WONE; cu = cu * wv_pi() / WONE; cu = cu * wv_pi() / WONE; cu = cu * 512 / 6;
        WT[n].c3 = (n == 16) ? 0 : (i64)((cu + WONE/2) / WONE);
        // square's sine field: normalised to [2^(w-1), 2^w), octave k
        int k = 0; if (n > 0) while (s * ((wi128)1 << (k+1)) < WONE) k++;
        WT[n].k = k;
        if (n == 0) { WT[n].S = 0; }
        else { i64 sf = wv_rn(s, sinw + k); if (sf >= (1ll << sinw)) sf = (1ll << sinw) - 1; WT[n].S = sf; }
        i64 r26 = wv_rn(s, 26); if (r26 >= (1ll << 26)) r26 = (1ll << 26) - 1;
        WT[n].rn26 = r26;
        WT[n].C0   = WV_C0[n];
    }
}

// ------------------------------------------------------------------ the model
struct WCfg {
    int  s1 = 23, s2 = 20;          // chain latch shifts
    i64  k1 = 1ll << 19, k2 = 1ll << 19;   // chain rounding constants
    int  u2base = 50;               // u2_n = (u2base - s2) + k_n
    i64  KC = 0x4ef145;
    int  sinw = 23;
};
static inline int wv_u2(const WCfg& C, int n) { return (C.u2base - C.s2) + WT[n].k; }

static inline i64 wv_I1(const WCfg& C, int t) { return (wv_arr(C.KC, (i64)t << 13) + C.k1) >> C.s1; }
static inline i64 wv_I2(const WCfg& C, int t) { i64 dx = (i64)t << 13; return (wv_arr(wv_I1(C,t), dx) + C.k2) >> C.s2; }

// odd/even split: base = C0 + s*T1 - s*T3  (everything except the square term)
static inline i64 wv_base(int n, int t, int flip) {
    const WTab& T = WT[n]; i64 dx = (i64)t << 13; i64 s = flip ? -1 : 1;
    i64 T1 = T.C1f ? wv_sm(wv_arr(T.C1f, dx) + WK, 23 - T.e1) : 0;
    i64 A3 = ((wv_arr(((i64)t*t) << 3, dx) + WK) >> 20) >> 3;
    i64 T3 = T.c3 ? wv_sm(wv_arr(T.c3 << 5, A3) + WK, 26) : 0;
    return T.C0 + s*T1 - s*T3;
}
static inline i64 wv_T2(const WCfg& C, int n, i64 J) {
    return WT[n].S ? wv_sm(wv_arr(J, WT[n].S) + WK, wv_u2(C, n)) : 0;
}
static inline i64 wv_V(const WCfg& C, int n, int t, int flip) {
    return wv_base(n, t, flip) - wv_T2(C, n, wv_I2(C, t));
}

// ------------------------------------------------------------------ dump windows
static Fsca WF;
static inline void wv_win(int a, i64& lo, i64& hi, bool& ex) {
    Win w = decode(WF.srz[a], WF.srn[a]);
    if (w.sign > 0) { lo = w.lo; hi = w.hi; } else { lo = -w.hi; hi = -w.lo; }
    ex = w.exact;
}
