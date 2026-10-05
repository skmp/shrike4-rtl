// v32_common.h -- ADVERSARIAL VERIFIER branch 32.  Written from scratch against base2.h + b39.cpp.
// Parameterised Booth digit count, plus an HONEST (bisection-free) latch-window rule.
#pragma once
#include "base2.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

// ---- Booth array with ND digits.  ND=14 must be bit-identical to base2.h arr().
static i64 arrN(i64 rec, i64 mul, int ND) {
    i64 R[40]; i64 T = 0;
    for (int k = 0; k < ND; k++) {
        auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); };
        int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag;
        R[k] = row * (1ll << (2*k));
        T += (R[k] >> 16) << 16;
        if (d < 0 && 2*k >= 18) T += 1ll << (2*k);
    }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2);
    int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16;
    return T;
}

static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};

static Fsca FD;
static inline void win_of(int a, i64& lo, i64& hi){ Win w = decode(FD.srz[a], FD.srn[a]);
    if (w.sign > 0) { lo = w.lo; hi = w.hi; } else { lo = -w.hi; hi = -w.lo; } }

// ---- model pieces; ND applied to EVERY array call ("globally") ----
static inline i64 T1_of(int n, i64 dx, int nd){ return TB[n].C1f ? sm(arrN(TB[n].C1f, dx, nd) + K, 23 - TB[n].e1) : 0; }
static inline i64 A3_of(int t, i64 dx, int nd){ return ((arrN(((i64)t*t) << 3, dx, nd) + K) >> 20) >> 3; }
static inline i64 T3_of(int n, i64 A3, int nd){ return TB[n].c3 ? sm(arrN(TB[n].c3 << 5, A3, nd) + K, 26) : 0; }
// square chain; swap=1 uses arr(dx,KC) for the first multiply
static inline i64 J_of(int t, int nd, int s2, int swap){ i64 dx = (i64)t << 13;
    i64 I1 = ((swap ? arrN(dx, KC, nd) : arrN(KC, dx, nd)) + K) >> 23;
    return (arrN(I1, dx, nd) + K) >> s2; }
// full internal value with an explicit J and latch scale DU (0 = coarse/published, 2 = fine)
static inline i64 V_of(int node, int t, i64 J, int nd, int DU){
    int flip = node >= 17, n = flip ? 32 - node : node; i64 s = flip ? -1 : 1; i64 dx = (i64)t << 13;
    i64 T1 = T1_of(n, dx, nd); i64 T3 = T3_of(n, A3_of(t, dx, nd), nd);
    i64 T2 = TB[n].S ? sm(arrN(J, TB[n].S, nd) + K, U2TAB[n] + DU) : 0;
    return C0TAB[n] + s*T1 - T2 - s*T3; }

// jwin18.txt as published
static i64 FL[1024], FH[1024];
static bool load_jwin(const char* p = "jwin18.txt"){ FILE* f = fopen(p,"r"); if(!f) return false;
    char ln[256]; int got = 0;
    while (fgets(ln, sizeof ln, f)) { if (ln[0]=='#') continue; long long t,l,h;
        if (sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3 && t>=-512 && t<=511) { FL[t+512]=l; FH[t+512]=h; got++; } }
    fclose(f); return got == 1024; }
