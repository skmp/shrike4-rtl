// wv_chain_win.cpp -- independent regeneration of the J-window dataset by DIRECT SCAN
// (no monotonicity assumption, no binary search), and the witness rows.
#include "base2.h"
#include <cstring>
#include <cstdio>
#include <vector>
static Fsca F;
static inline void win_of(int a, i64& lo, i64& hi) { Win w = decode(F.srz[a], F.srn[a]);
    if (w.sign > 0) { lo = w.lo; hi = w.hi; } else { lo = -w.hi; hi = -w.lo; } }
static i64 HLO[65536], HHI[65536];
// the C0/u2 table used by b12.cpp when it wrote jwin.txt -- recomputed here, not copied:
// it is the argmin of the joint (C0,u2) fit of the BASELINE model, which wv_chain_score reproduced.
static i64 C0T[17]; static int U2T[17];
static i64 JLf[1024], JHf[1024]; static int FEf[1024];
int main() {
    if (!load_fsca(F)) return 1; build_tables();
    for (int a = 0; a < 65536; a++) win_of(a, HLO[a], HHI[a]);
    // --- recompute the baseline (C0,u2) fit exactly as b2.cpp does
    for (int n = 0; n <= 16; n++) { const Tab& T = TB[n];
        int nodes[2] = { n, 32 - n }; int nn = (n == 0 || n == 16) ? 1 : 2;
        i64 lo0, hi0; win_of((n*1024) & 0xffff, lo0, hi0);
        int bv = 1 << 30; i64 bc = lo0; int bu = T.u2;
        for (i64 c = lo0 - 3; c <= hi0 + 3; c++) for (int u = T.u2 - 1; u <= T.u2 + 1; u++) { int v = 0;
            for (int i = 0; i < nn; i++) { int m = nodes[i]; int flip = m >= 17; i64 s = flip ? -1 : 1;
                for (int t = -512; t <= 511; t++) { i64 dx = (i64)t << 13;
                    i64 T1 = T.C1f ? sm(arr(T.C1f, dx) + K, 23 - T.e1) : 0;
                    i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
                    i64 T3 = T.c3 ? sm(arr(T.c3 << 5, A3) + K, 26) : 0;
                    i64 I1 = (arr(KC, dx) + K) >> 23, I2 = (arr(I1, dx) + K) >> 20;
                    i64 T2 = T.S ? sm(arr(I2, T.S) + K, u) : 0;
                    i64 V = c + s*T1 - T2 - s*T3; int a = (m*1024 + t) & 0xffff;
                    if (V < HLO[a] || V > HHI[a]) v++; } }
            if (v < bv) { bv = v; bc = c; bu = u; } }
        C0T[n] = bc; U2T[n] = bu; }
    printf("C0 fit: "); for (int n = 0; n <= 16; n++) printf("%lld%s", (long long)C0T[n], n<16?",":"\n");
    printf("u2 fit: "); for (int n = 0; n <= 16; n++) printf("%d%s", U2T[n], n<16?",":"\n");
    printf("u2 derived (30+k): "); for (int n = 0; n <= 16; n++) printf("%d%s", TB[n].u2, n<16?",":"\n");
    // --- windows by DIRECT SCAN over a wide bracket
    #pragma omp parallel for schedule(dynamic)
    for (int t = -512; t <= 511; t++) {
        i64 dx = (i64)t << 13;
        i64 ideal = ((i64)0x4ef145 * t * t) >> 17;
        i64 lo = ideal - 6000, hi = ideal + 6000; if (lo < 0) lo = 0;
        i64 fmin = -1, fmax = -1; int holes = 0; bool prev = false;
        for (i64 J = lo; J <= hi; J++) { bool ok = true;
            for (int node = 1; node <= 31 && ok; node++) { int flip = node >= 31 ? 1 : (node >= 17);
                flip = node >= 17; int n = flip ? 32 - node : node; const Tab& T = TB[n]; if (!T.S) continue;
                i64 s = flip ? -1 : 1;
                i64 T1 = T.C1f ? sm(arr(T.C1f, dx) + K, 23 - T.e1) : 0;
                i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
                i64 T3 = T.c3 ? sm(arr(T.c3 << 5, A3) + K, 26) : 0;
                i64 T2 = sm(arr(J, T.S) + K, U2T[n]);
                i64 V = C0T[n] + s*T1 - T2 - s*T3; int a = (node*1024 + t) & 0xffff;
                if (V < HLO[a] || V > HHI[a]) ok = false; }
            if (ok) { if (fmin < 0) fmin = J; fmax = J; if (!prev && fmin >= 0 && J != fmin) holes++; }
            prev = ok; }
        JLf[t+512] = fmin; JHf[t+512] = fmax; FEf[t+512] = fmin >= 0;
        if (fmin >= 0 && (fmin <= lo || fmax >= hi)) { printf("t=%d bracket too tight\n", t); }
    }
    // --- compare with jwin.txt
    FILE* f = fopen("jwin.txt", "r"); int diff = 0, feas = 0;
    for (int i = 0; i < 1024; i++) { int t, fe; long long a, b; fscanf(f, "%d %lld %lld %d", &t, &a, &b, &fe);
        feas += FEf[i];
        if (FEf[i] != fe || (fe && (a != JLf[i] || b != JHf[i])))
            { if (diff < 12) printf("DIFF t=%d file %lld %lld %d  mine %lld %lld %d\n", t, a, b, fe,
                 (long long)JLf[i], (long long)JHf[i], FEf[i]); diff++; } }
    fclose(f);
    printf("independent scan vs jwin.txt: %d differences, %d feasible\n", diff, feas);
    int wt[5] = {-342, 342, 376, -376, 258};
    for (int i = 0; i < 5; i++) { int t = wt[i]; printf("witness t=%4d  window [%lld, %lld] width %lld\n",
        t, (long long)JLf[t+512], (long long)JHf[t+512], (long long)(JHf[t+512]-JLf[t+512])); }
    return 0; }
