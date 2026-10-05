// wv_tables_sweep.cpp
//  I. is the "scale law" a pure refinement?  test  sm(arr(J,S)+K,u) == sm(arr(4J,S)+K,u+2)  exhaustively
//  J. best achievable M1 for each latch shift over every chain rounding/shift/operand-order choice:
//     if the best s2=20 configuration is at least as good as the best s2=18 one, the finer latch buys nothing
#include "wv_tables_core.h"
#include <cstdio>
#include <omp.h>

static inline i64 I1_ord(const WCfg& C, int t, int ord) {
    i64 dx = (i64)t << 13;
    return ((ord ? wv_arr(dx, C.KC) : wv_arr(C.KC, dx)) + C.k1) >> C.s1;
}
static inline i64 I2_ord(const WCfg& C, int t, int ord, int ord2) {
    i64 dx = (i64)t << 13; i64 I1 = I1_ord(C, t, ord);
    return ((ord2 ? wv_arr(dx, I1) : wv_arr(I1, dx)) + C.k2) >> C.s2;
}
static int M1_of(const WCfg& C, int ord, int ord2, int* pn = nullptr) {
    int m1 = 0;
    for (int node = 0; node <= 31; node++) { int flip = node >= 17, n = flip ? 32-node : node;
        i64 prevJ = -1;
        for (int t = -512; t <= 511; t++) {
            int a = (node*1024+t) & 0xffff; i64 lo, hi; bool ex; wv_win(a, lo, hi, ex);
            i64 V = wv_base(n, t, flip) - wv_T2(C, n, I2_ord(C, t, ord, ord2));
            if (V < lo || V > hi) { m1++; if (pn) pn[node]++; }
        } (void)prevJ; }
    return m1;
}

int main() {
    if (!load_fsca(WF)) { puts("dump load failed"); return 1; }
    wv_build();

    puts("=== I. is s2=18/u2=32+k an exact refinement of s2=20/u2=30+k? ===");
    puts("    (if sm(arr(J,S)+K,u) == sm(arr(4J,S)+K,u+2) always, the change is PURELY a finer grid)");
    for (int n = 1; n <= 16; n += 3) {
        long long bad = 0, tot = 0; i64 maxd = 0;
        for (i64 J = 0; J <= 10400000; J++) {
            i64 a = wv_sm(wv_arr(J, WT[n].S) + WK, 30 + WT[n].k);
            i64 b = wv_sm(wv_arr(4*J, WT[n].S) + WK, 32 + WT[n].k);
            if (a != b) { bad++; i64 d = a > b ? a-b : b-a; if (d > maxd) maxd = d; } tot++;
        }
        printf("    table n=%2d (S=%8lld k=%d): %lld of %lld J differ, max |difference| %lld\n",
               n, (long long)WT[n].S, WT[n].k, bad, tot, (long long)maxd);
    }

    puts("\n=== J. best M1 for each latch shift, over the whole chain-parameter family ===");
    puts("    family: s1 in {21..25}, k1,k2 in {0, 2^16, 2^17, 2^18, 2^19}, both operand orders in both multiplies");
    puts("    s2   best M1   at (s1,k1,k2,ord1,ord2)                guard 0,1,2,16,30,31   max latch bits");
    for (int s2 = 21; s2 >= 17; s2--) {
        int best = 1 << 30; WCfg bc; int bo1 = 0, bo2 = 0;
        const i64 ks[5] = {0, 1ll<<16, 1ll<<17, 1ll<<18, 1ll<<19};
        #pragma omp parallel for collapse(2) schedule(dynamic)
        for (int s1 = 21; s1 <= 25; s1++) for (int i1 = 0; i1 < 5; i1++) {
            for (int i2 = 0; i2 < 5; i2++) for (int o1 = 0; o1 < 2; o1++) for (int o2 = 0; o2 < 2; o2++) {
                WCfg C; C.s2 = s2; C.s1 = s1; C.k1 = ks[i1]; C.k2 = ks[i2];
                int m = M1_of(C, o1, o2);
                #pragma omp critical
                if (m < best) { best = m; bc = C; bo1 = o1; bo2 = o2; }
            } }
        int pn[32]; memset(pn, 0, sizeof(pn)); M1_of(bc, bo1, bo2, pn);
        i64 mx = 0; for (int t = -512; t <= 511; t++) { i64 v = I2_ord(bc, t, bo1, bo2); if (v > mx) mx = v; }
        int bits = 0; { i64 m = mx; while (m) { bits++; m >>= 1; } }
        printf("    %2d    %4d     s1=%d k1=%-7lld k2=%-7lld ord=%d,%d       %d,%d,%d,%d,%d,%d            %d\n",
            s2, best, bc.s1, (long long)bc.k1, (long long)bc.k2, bo1, bo2,
            pn[0],pn[1],pn[2],pn[16],pn[30],pn[31], bits);
        fflush(stdout);
    }
    return 0;
}
