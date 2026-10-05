// w3_common.h -- branch 3 harness: "how is t presented to the array?"
// Parameterised Booth digit count, parameterised operand formation, window regeneration,
// jwin18 scoring and full-model violation counting.  Everything integer.
#pragma once
#include "base2.h"
#include <cstring>
#include <cstdio>

// ---- Booth array with a parameterised digit count (14 == the validated FIPR array) ----
static i64 arrN(i64 rec, i64 mul, int ND) {
    i64 R[40]; i64 T = 0;
    for (int k = 0; k < ND; k++) {
        auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); };
        int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag;
        R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16;
        if (d < 0 && 2*k >= 18) T += 1ll << (2*k);
    }
    if (ND >= 3) { int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); }
    if (ND >= 9) { int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; }
    return T;
}
// signed range the recoded operand must live in for an ND-digit array to see it whole
static inline bool rec_fits(i64 rec, int ND) { i64 lim = 1ll << (2*ND - 1); return rec >= -lim && rec < lim; }

// ---- pinned tables (from b39.cpp; C0 uniquely pinned per CURRENT.md 3) ----
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17] = {30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static int DU = 2;                    // fine latch: u2 + 2 (shift 18 chain), matches jwin18.txt

static Fsca FD;
static inline void win_of(int a, i64& lo, i64& hi) { Win w = decode(FD.srz[a], FD.srn[a]);
    if (w.sign > 0) { lo = w.lo; hi = w.hi; } else { lo = -w.hi; hi = -w.lo; } }

// ---- per-experiment array-width knobs (14 everywhere == baseline) ----
static int ND_T1 = 14, ND_T3a = 14, ND_T3b = 14, ND_SQ1 = 14, ND_SQ2 = 14, ND_T2 = 14;

// ---- baseline square chain, with a pluggable operand former for dx in each multiply ----
static inline i64 chain_J(i64 dx1, i64 dx2, int s1 = 23, int s2 = 18) {
    i64 I1 = (arrN(KC, dx1, ND_SQ1) + K) >> s1;
    return (arrN(I1, dx2, ND_SQ2) + K) >> s2;
}
static inline i64 baseline_J(int t) { i64 dx = (i64)t << 13; return chain_J(dx, dx); }

// ---- regenerate the latch window [JL,JH] per t, for the current ND_* settings ----
// A J-rule is scored against these.  LIM bounds the binary search.
static i64 JLw[1024], JHw[1024]; static bool JFeas[1024];
static void gen_windows() {
    const i64 LIM = 12000000ll << DU;
    for (int t = -512; t <= 511; t++) {
        i64 dx = (i64)t << 13; i64 gl = 0, gh = LIM;
        i64 A3 = ((arrN(((i64)t*t) << 3, dx, ND_T3a) + K) >> 20) >> 3;
        for (int node = 1; node <= 31; node++) {
            int flip = node >= 17, n = flip ? 32 - node : node; if (!TB[n].S) continue;
            const Tab& T = TB[n]; i64 s = flip ? -1 : 1;
            i64 T1 = T.C1f ? sm(arrN(T.C1f, dx, ND_T1) + K, 23 - T.e1) : 0;
            i64 T3 = T.c3 ? sm(arrN(T.c3 << 5, A3, ND_T3b) + K, 26) : 0;
            i64 base = C0TAB[n] + s*T1 - s*T3; int a = (node*1024 + t) & 0xffff; i64 lo, hi; win_of(a, lo, hi);
            i64 t2lo = base - hi, t2hi = base - lo; int u = U2TAB[n] + DU;
            i64 a1 = 0, b1 = LIM+1; while (a1 < b1) { i64 m = (a1+b1)/2; if (sm(arrN(m, T.S, ND_T2) + K, u) >= t2lo) b1 = m; else a1 = m+1; }
            i64 a2 = -1, b2 = LIM;  while (a2 < b2) { i64 m = (a2+b2+1)/2; if (sm(arrN(m, T.S, ND_T2) + K, u) <= t2hi) a2 = m; else b2 = m-1; }
            if (a1 > gl) gl = a1; if (a2 < gh) gh = a2;
        }
        JLw[t+512] = gl; JHw[t+512] = gh; JFeas[t+512] = gl <= gh;
    }
}
// load the published jwin18.txt (identical to gen_windows() at default ND)
static i64 JLf[1024], JHf[1024];
static bool load_jwin(const char* p = "jwin18.txt") {
    FILE* f = fopen(p, "r"); if (!f) return false; char ln[256];
    while (fgets(ln, sizeof ln, f)) { if (ln[0]=='#') continue; long long t,l,h;
        if (sscanf(ln, "%lld %lld %lld", &t, &l, &h) == 3) { JLf[t+512]=l; JHf[t+512]=h; } }
    fclose(f); return true;
}
template <class F> static int score_jwin(F Jf, const i64* L, const i64* H) {
    int s = 0; for (int i = 0; i < 1024; i++) { i64 j = Jf(i - 512); if (j >= L[i] && j <= H[i]) s++; } return s;
}
// ---- full model violation count over nodes 0..31 with a pluggable J(t) ----
template <class F> static int full_violations(F Jf, int* g0 = nullptr) {
    int v = 0; int guard = 0;
    for (int node = 0; node <= 31; node++) {
        int flip = node >= 17, n = flip ? 32 - node : node; const Tab& T = TB[n]; i64 s = flip ? -1 : 1;
        for (int t = -512; t <= 511; t++) {
            i64 dx = (i64)t << 13;
            i64 T1 = T.C1f ? sm(arrN(T.C1f, dx, ND_T1) + K, 23 - T.e1) : 0;
            i64 A3 = ((arrN(((i64)t*t) << 3, dx, ND_T3a) + K) >> 20) >> 3;
            i64 T3 = T.c3 ? sm(arrN(T.c3 << 5, A3, ND_T3b) + K, 26) : 0;
            i64 J  = Jf(t);
            i64 T2 = T.S ? sm(arrN(J, T.S, ND_T2) + K, U2TAB[n] + DU) : 0;
            i64 V = C0TAB[n] + s*T1 - T2 - s*T3;
            int a = (node*1024 + t) & 0xffff; i64 lo, hi; win_of(a, lo, hi);
            if (V < lo || V > hi) { v++; if (node==0||node==1||node==2||node==16) guard++; }
        }
    }
    if (g0) *g0 = guard; return v;
}
static const int PIN_T[3] = {17, 342, -342};
template <class F> static void report_pins(const char* tag, F Jf, const i64* L, const i64* H) {
    printf("  %-28s J(17)=%lld [%lld,%lld]%s  J(342)=%lld [%lld,%lld]%s  J(-342)=%lld [%lld,%lld]%s\n", tag,
      (long long)Jf(17),(long long)L[17+512],(long long)H[17+512], (Jf(17)>=L[17+512]&&Jf(17)<=H[17+512])?" HIT":"",
      (long long)Jf(342),(long long)L[342+512],(long long)H[342+512], (Jf(342)>=L[342+512]&&Jf(342)<=H[342+512])?" HIT":"",
      (long long)Jf(-342),(long long)L[-342+512],(long long)H[-342+512], (Jf(-342)>=L[-342+512]&&Jf(-342)<=H[-342+512])?" HIT":"");
}

// full model with a NODE-DEPENDENT J(node,t) -- needed for hypotheses where the square path's
// operand is not the node-relative t (branch 3d).  jwin18 cannot score those: its contract is a
// single J shared by all 31 node instances.
template <class F> static int full_violations_nt(F Jf, int* g0 = nullptr) {
    int v = 0, guard = 0;
    for (int node = 0; node <= 31; node++) {
        int flip = node >= 17, n = flip ? 32 - node : node; const Tab& T = TB[n]; i64 s = flip ? -1 : 1;
        for (int t = -512; t <= 511; t++) {
            i64 dx = (i64)t << 13;
            i64 T1 = T.C1f ? sm(arrN(T.C1f, dx, ND_T1) + K, 23 - T.e1) : 0;
            i64 A3 = ((arrN(((i64)t*t) << 3, dx, ND_T3a) + K) >> 20) >> 3;
            i64 T3 = T.c3 ? sm(arrN(T.c3 << 5, A3, ND_T3b) + K, 26) : 0;
            i64 J  = Jf(node, t);
            i64 T2 = T.S ? sm(arrN(J, T.S, ND_T2) + K, U2TAB[n] + DU) : 0;
            i64 V = C0TAB[n] + s*T1 - T2 - s*T3;
            int a = (node*1024 + t) & 0xffff; i64 lo, hi; win_of(a, lo, hi);
            if (V < lo || V > hi) { v++; if (node==0||node==1||node==2||node==16) guard++; }
        }
    }
    if (g0) *g0 = guard; return v;
}
