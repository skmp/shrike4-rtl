// wv_tables_probe.cpp -- the adversarial probes.
//  A. brute-force (no monotonicity, no inversion) check of t = +-17 at s2 = 18 and s2 = 20
//  B. does the CHAIN actually produce the required latch at t = +-17, or only a free latch?
//  C. granularity control: is "every t feasible" a property of column 18, or of ANY finer quantisation?
//     - run s2 = 14..21 for M2/M3
//     - run s2 = 20 with the latch artificially refined by a factor 2^g (same value, finer grid)
//  D. operand width: what happens if the multiplier's recoded-operand latch is really 24 bits
//     (the width the FIPR characterisation established) instead of 26?
#include "wv_tables_core.h"
#include <cstdio>
#include <omp.h>

static i64 JMAXF(const WCfg& C) { i64 mx = 0; for (int t = -512; t <= 511; t++) { i64 v = wv_I2(C,t); if (v > mx) mx = v; } return mx + mx/5 + 1000; }

// exact allowed-latch interval at one t (binary-search inversion; monotonicity verified exhaustively)
static void window_at(const WCfg& C, int t, i64& gl, i64& gh, i64 JMAX) {
    gl = 0; gh = JMAX;
    for (int node = 1; node <= 31; node++) {
        int flip = node >= 17, n = flip ? 32-node : node; if (!WT[n].S) continue;
        i64 base = wv_base(n, t, flip);
        int a = (node*1024 + t) & 0xffff; i64 lo, hi; bool ex; wv_win(a, lo, hi, ex);
        i64 t2lo = base - hi, t2hi = base - lo; int u = wv_u2(C, n);
        i64 a1 = 0, b1 = JMAX+1; while (a1 < b1) { i64 m=(a1+b1)/2; if (wv_sm(wv_arr(m,WT[n].S)+WK,u) >= t2lo) b1=m; else a1=m+1; }
        i64 a2 = -1, b2 = JMAX;   while (a2 < b2) { i64 m=(a2+b2+1)/2; if (wv_sm(wv_arr(m,WT[n].S)+WK,u) <= t2hi) a2=m; else b2=m-1; }
        if (a1 > gl) gl = a1; if (a2 < gh) gh = a2;
    }
}
// brute force: total violations over the 31 node instances at one t for one latch value
static int viol_at(const WCfg& C, int t, i64 J) {
    int v = 0;
    for (int node = 1; node <= 31; node++) {
        int flip = node >= 17, n = flip ? 32-node : node; if (!WT[n].S) continue;
        i64 V = wv_base(n, t, flip) - wv_T2(C, n, J);
        int a = (node*1024 + t) & 0xffff; i64 lo, hi; bool ex; wv_win(a, lo, hi, ex);
        if (V < lo || V > hi) v++;
    }
    return v;
}

int main() {
    if (!load_fsca(WF)) { puts("dump load failed"); return 1; }
    wv_build();

    puts("=== A. brute force at t = +-17 (every latch value scanned, no inversion, no monotonicity) ===");
    for (int s2 : {20, 19, 18, 17}) {
        WCfg C; C.s2 = s2;
        for (int t : {17, -17}) {
            i64 ideal = (i64)(39.4712L * t * t * (long double)(1ll << (20 - s2)));
            i64 lo = ideal > 20000 ? ideal - 20000 : 0, hi = ideal + 20000;
            int best = 1<<30; i64 bj = -1, bjhi = -1;
            for (i64 J = lo; J <= hi; J++) { int v = viol_at(C, t, J);
                if (v < best) { best = v; bj = J; bjhi = J; } else if (v == best) bjhi = J; }
            i64 chain = wv_I2(C, t);
            printf("  s2=%2d t=%+3d : min violations %d  first/last best latch %lld..%lld   chain latch %lld  (chain violations %d)\n",
                   s2, t, best, (long long)bj, (long long)bjhi, (long long)chain, viol_at(C, t, chain));
        }
    }

    puts("\n=== B. does the chain hit the required latch at the tight t?  (s2=18) ===");
    { WCfg C; C.s2 = 18; C.k2 = 0; i64 JMAX = JMAXF(C);
      for (int t : {17, -17, 342, -342}) { i64 gl, gh; window_at(C, t, gl, gh, JMAX);
        printf("  t=%+4d  allowed [%lld, %lld]  width %lld   chain(k2=0) %lld  -> %s\n", t,
            (long long)gl, (long long)gh, (long long)(gh-gl+1), (long long)wv_I2(C,t),
            (wv_I2(C,t) >= gl && wv_I2(C,t) <= gh) ? "inside" : "OUTSIDE"); }
      C.k2 = 1ll<<19;
      for (int t : {17, -17, 342, -342}) { i64 gl, gh; window_at(C, t, gl, gh, JMAX);
        printf("  t=%+4d  allowed [%lld, %lld]                chain(k2=2^19) %lld  -> %s\n", t,
            (long long)gl, (long long)gh, (long long)wv_I2(C,t),
            (wv_I2(C,t) >= gl && wv_I2(C,t) <= gh) ? "inside" : "OUTSIDE"); }
    }

    puts("\n=== C. granularity control: M2/M3 as a function of the latch shift alone ===");
    puts("  s2  u2base-s2  max|latch|   bits  infeasible t (M3)  free-latch violations (M2)  mean window width");
    for (int s2 = 14; s2 <= 22; s2++) {
        WCfg C; C.s2 = s2; i64 JMAX = JMAXF(C);
        int m3 = 0, m2 = 0; long long widths = 0; i64 mx = 0;
        #pragma omp parallel for schedule(dynamic) reduction(+:m3,m2,widths)
        for (int ti = 0; ti < 1024; ti++) { int t = ti - 512; i64 gl, gh; window_at(C, t, gl, gh, JMAX);
            if (gl > gh) { m3++;
                int best = 1<<30; i64 c = (gl+gh)/2;
                for (i64 J = (c > 4000 ? c-4000 : 0); J <= c+4000; J++) { int v = viol_at(C,t,J); if (v<best) best=v; }
                m2 += best;
            } else widths += gh - gl + 1; }
        for (int t = -512; t <= 511; t++) { i64 v = wv_I2(C,t); if (v > mx) mx = v; }
        int bits = 0; { i64 m = mx; while (m) { bits++; m >>= 1; } }
        printf("  %2d      %2d      %10lld  %3d       %3d                 %3d                 %.2f\n",
               s2, 50 - s2, (long long)mx, bits, m3, m2, (double)widths / (1024 - m3));
    }

    puts("\n=== D. operand width: the FIPR multiplier is 24x24.  Mask the latch to 24 bits at each s2 ===");
    puts("  s2   t with latch >= 2^24   M1 with an honest 24-bit operand latch   M1 with an unbounded latch");
    for (int s2 = 21; s2 >= 17; s2--) {
        WCfg C; C.s2 = s2; C.k2 = 0;
        int over = 0, m1_mask = 0, m1_free = 0;
        for (int node = 0; node <= 31; node++) { int flip = node >= 17, n = flip ? 32-node : node;
            for (int t = -512; t <= 511; t++) {
                int a = (node*1024+t) & 0xffff; i64 lo, hi; bool ex; wv_win(a, lo, hi, ex);
                i64 J = wv_I2(C, t);
                i64 Vf = wv_base(n,t,flip) - wv_T2(C, n, J);
                i64 Vm = wv_base(n,t,flip) - wv_T2(C, n, J & ((1ll<<24)-1));
                if (Vf < lo || Vf > hi) m1_free++;
                if (Vm < lo || Vm > hi) m1_mask++;
            } }
        for (int t = -512; t <= 511; t++) if (wv_I2(C,t) >= (1ll<<24)) over++;
        printf("  %2d        %4d                 %6d                                 %4d\n", s2, over, m1_mask, m1_free);
    }
    return 0;
}
