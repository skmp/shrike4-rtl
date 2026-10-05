// wv_tables_main.cpp -- independent measurement of the four metrics for any square-path configuration.
//   M1 = window violations over nodes 0..31 (32768 samples) with the chain latch
//   M2 = minimum window violations with a FREE integer latch per t (one value shared by all node instances)
//   M3 = number of t for which no integer latch satisfies every node instance
//   M4 = rule score: feasible t for which the chain's latch lands inside the allowed window
// Every decision is integer.  Binary-search inversion is only used after monotonicity of T2(J)
// has been verified exhaustively over the search range (phase "mono").
#include "wv_tables_core.h"
#include <cstdio>
#include <omp.h>

static i64 JMAXF(const WCfg& C) {            // search ceiling: 20% above the largest chain latch
    i64 mx = 0; for (int t = -512; t <= 511; t++) { i64 v = wv_I2(C, t); if (v > mx) mx = v; }
    return mx + mx/5 + 1000;
}

struct Res { int M1, M2, M3, M4; int pernode[32]; long long absdist; };

static Res measure(const WCfg& C, bool doFree, i64* JLo = nullptr, i64* JHi = nullptr, i64* Jchain = nullptr) {
    Res R; memset(&R, 0, sizeof(R));
    const i64 JMAX = JMAXF(C);
    // ---- M1 and per-node counts: direct evaluation against the dump windows
    for (int node = 0; node <= 31; node++) {
        int flip = node >= 17, n = flip ? 32 - node : node;
        for (int t = -512; t <= 511; t++) {
            int a = (node*1024 + t) & 0xffff; i64 lo, hi; bool ex; wv_win(a, lo, hi, ex);
            i64 V = wv_V(C, n, t, flip);
            if (V < lo || V > hi) { R.M1++; R.pernode[node]++; }
        }
    }
    int m2 = 0, m3 = 0, m4 = 0; long long ad = 0;
    #pragma omp parallel for schedule(dynamic) reduction(+:m2,m3,m4,ad)
    for (int ti = 0; ti < 1024; ti++) {
        int t = ti - 512;
        i64 gl = 0, gh = JMAX;
        i64 clo[32], chi[32], cS[32], tlo[32], thi[32]; int cu[32], nc = 0;
        for (int node = 1; node <= 31; node++) {
            int flip = node >= 17, n = flip ? 32 - node : node;
            if (!WT[n].S) continue;
            i64 base = wv_base(n, t, flip);
            int a = (node*1024 + t) & 0xffff; i64 lo, hi; bool ex; wv_win(a, lo, hi, ex);
            i64 t2lo = base - hi, t2hi = base - lo; int u = wv_u2(C, n);
            i64 a1 = 0, b1 = JMAX + 1;                       // smallest J with T2 >= t2lo
            while (a1 < b1) { i64 m = (a1 + b1) / 2; if (wv_sm(wv_arr(m, WT[n].S) + WK, u) >= t2lo) b1 = m; else a1 = m + 1; }
            i64 a2 = -1, b2 = JMAX;                          // largest J with T2 <= t2hi
            while (a2 < b2) { i64 m = (a2 + b2 + 1) / 2; if (wv_sm(wv_arr(m, WT[n].S) + WK, u) <= t2hi) a2 = m; else b2 = m - 1; }
            clo[nc] = a1; chi[nc] = a2; cS[nc] = WT[n].S; cu[nc] = u; tlo[nc] = t2lo; thi[nc] = t2hi; nc++;
            if (a1 > gl) gl = a1; if (a2 < gh) gh = a2;
        }
        i64 Jm = wv_I2(C, t);
        if (JLo) JLo[ti] = gl; if (JHi) JHi[ti] = gh; if (Jchain) Jchain[ti] = Jm;
        if (gl > gh) {
            m3++;
            if (doFree) {   // minimum violations over J; breakpoints are the interval endpoints
                std::vector<i64> cand;
                for (int i = 0; i < nc; i++) { cand.push_back(clo[i]); cand.push_back(chi[i]);
                    cand.push_back(clo[i]-1); cand.push_back(chi[i]+1); }
                cand.push_back(Jm);
                std::sort(cand.begin(), cand.end()); cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
                int bv = 1 << 30;
                for (i64 j : cand) { if (j < 0 || j > JMAX) continue; int v = 0;
                    for (int i = 0; i < nc; i++) { i64 T2 = wv_sm(wv_arr(j, cS[i]) + WK, cu[i]);   // DIRECT, no monotonicity
                        if (T2 < tlo[i] || T2 > thi[i]) v++; }
                    if (v < bv) bv = v; }
                m2 += bv;
            }
        } else {
            if (Jm >= gl && Jm <= gh) m4++;
            else ad += (Jm < gl) ? (gl - Jm) : (Jm - gh);
        }
    }
    R.M2 = m2; R.M3 = m3; R.M4 = m4; R.absdist = ad;
    return R;
}

int main(int argc, char** argv) {
    if (!load_fsca(WF)) { puts("dump load failed"); return 1; }
    wv_build();
    const char* phase = argc > 1 ? argv[1] : "scan";

    if (!strcmp(phase, "mono")) {
        for (int s2 = 18; s2 <= 21; s2++) {
            WCfg C; C.s2 = s2; i64 JMAX = JMAXF(C);
            long long bad = 0;
            #pragma omp parallel for schedule(dynamic) reduction(+:bad)
            for (int n = 1; n <= 16; n++) {
                int u = wv_u2(C, n); i64 prev = wv_sm(wv_arr(0, WT[n].S) + WK, u);
                for (i64 J = 1; J <= JMAX; J++) { i64 v = wv_sm(wv_arr(J, WT[n].S) + WK, u); if (v < prev) bad++; prev = v; }
            }
            printf("mono s2=%d  JMAX=%lld  monotonicity violations over 16 tables: %lld\n", s2, (long long)JMAX, bad);
            fflush(stdout);
        }
        return 0;
    }

    puts("cfg                                M1   per-node 0,1,2,16,30,31    M2   M3   M4/feas    sum|dist|");
    struct Cf { int s2; i64 k1, k2; const char* name; };
    Cf list[] = {
        {20, 1ll<<19, 1ll<<19, "BASELINE s2=20 k1=2^19 k2=2^19"},
        {20, 1ll<<19, 0,       "CONTROL  s2=20 k1=2^19 k2=0   "},
        {19, 1ll<<19, 1ll<<19, "         s2=19 k1=2^19 k2=2^19"},
        {19, 1ll<<19, 0,       "         s2=19 k1=2^19 k2=0   "},
        {18, 1ll<<19, 1ll<<19, "CLAIM    s2=18 k1=2^19 k2=2^19"},
        {18, 1ll<<19, 0,       "CLAIM    s2=18 k1=2^19 k2=0   "},
        {21, 1ll<<19, 1ll<<19, "         s2=21 k1=2^19 k2=2^19"},
        {21, 1ll<<19, 0,       "         s2=21 k1=2^19 k2=0   "},
    };
    for (auto& c : list) {
        WCfg C; C.s2 = c.s2; C.k1 = c.k1; C.k2 = c.k2;
        Res R = measure(C, true);
        int feas = 1024 - R.M3;
        printf("%s %4d   %3d,%3d,%3d,%3d,%3d,%3d       %4d %4d  %4d/%4d %10lld\n", c.name, R.M1,
            R.pernode[0], R.pernode[1], R.pernode[2], R.pernode[16], R.pernode[30], R.pernode[31],
            R.M2, R.M3, R.M4, feas, R.absdist);
        fflush(stdout);
    }
    return 0;
}
