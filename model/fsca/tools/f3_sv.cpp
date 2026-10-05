// f3_sv.cpp -- is the coarse-latch result a discovery or an artefact of a coarser sine field?
// For each sine-field / alignment rule, report the COARSE free-J floor twice: with the published final multiply
// and with the best complement-feedback form.  If the floor falls monotonically with the field width under BOTH
// columns, the effect is precision loss.  If it falls only in the feedback column, the feedback is doing it.
#include "f3_fb.h"
#include "f3_field.h"
#include <cstdio>
static Fsca DUMP;
static i64 WLO[65536], WHI[65536];
static void measure(int sv, const FbV& v, int du, i64 RR, int& feas, long& flo, std::vector<int>& bad) {
    int nr = fb_nrows(v); i64 S[17]; int U[17];
    for (int n = 0; n <= 16; n++) { fin_field2(sv, n, S[n], U[n]); U[n] += du; }
    i64 b0[17]; for (int n = 0; n <= 16; n++) b0[n] = fb_base0(v, S[n], U[n], nr);
    feas = 0; flo = 0; bad.clear();
    for (int t = -512; t <= 511; t++) {
        i64 dx = (i64)t << 13; i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
        std::vector<NodeCon> cons;
        for (int node = 1; node <= 31; node++) { int flip = node >= 17, n = flip ? 32 - node : node;
            if (!S[n]) continue; i64 s = flip ? -1 : 1;
            i64 T1 = TB[n].C1f ? sm(arr(TB[n].C1f, dx) + K, 23 - TB[n].e1) : 0;
            i64 T3 = TB[n].c3 ? sm(arr(TB[n].c3 << 5, A3) + K, 26) : 0;
            i64 base = FB_C0[n] + s * T1 - s * T3; int a = (node * 1024 + t) & 0xffff;
            NodeCon c; c.n = n; c.clo = WLO[a] - base; c.chi = WHI[a] - base; c.S = S[n]; c.u = U[n]; c.base0 = b0[n];
            cons.push_back(c); }
        std::sort(cons.begin(), cons.end(), [](const NodeCon& a, const NodeCon& b){ return a.chi-a.clo < b.chi-b.clo; });
        i64 c0 = fb_ideal(t, du); int bv = 1 << 30;
        for (i64 J = std::max((i64)0, c0 - RR); J <= c0 + RR; J++) { int viol = 0;
            for (const NodeCon& q : cons) { i64 g = fb_contrib(v, J, q.S, q.u, nr, q.base0);
                if (g < q.clo || g > q.chi) { if (++viol >= bv) break; } }
            if (viol < bv) bv = viol; if (!bv) break; }
        if (!bv) feas++; else { flo += bv; if (bad.size() < 6) bad.push_back(t); }
    }
}
int main() {
    if (!load_fsca(DUMP)) { puts("dump load failed"); return 1; } build_tables(); FBF = DUMP;
    for (int a = 0; a < 65536; a++) { Win w = decode(DUMP.srz[a], DUMP.srn[a]);
        if (w.sign > 0) { WLO[a] = w.lo; WHI[a] = w.hi; } else { WLO[a] = -w.hi; WHI[a] = -w.lo; } }
    puts("COARSE latch (shift 20, the scale the published u = 30 + k implies).  free-J floor, per sine rule.");
    printf("%-38s | %-28s | %-34s\n", "sine field / alignment rule", "published final multiply",
           "best complement-feedback form");
    printf("%-38s | %-28s | %-34s\n", "", "feasible  floor  first bad", "feasible  floor  first bad   (variant)");
    std::vector<FbV> vs;
    for (int comp = 0; comp < 2; comp++) for (int tm = 0; tm < 3; tm++) for (int role = 0; role < 2; role++)
        for (int k3 = 0; k3 < 3; k3++) for (int w = 23; w <= 34; w++) vs.push_back(FbV{MODE_SEP, comp, tm, role, k3, w});
    for (int sv = 0; sv < NSV2; sv++) {
        int f1; long l1; std::vector<int> b1; FbV cur{MODE_CUR, 0, 0, 0, 1, 0};
        measure(sv, cur, 0, 600, f1, l1, b1);
        long bl = 1 << 30; int bf = 0; std::vector<int> bb; FbV bv{};
        std::vector<long> ls(vs.size()); std::vector<int> fs(vs.size()); std::vector<std::vector<int>> bs(vs.size());
        #pragma omp parallel for schedule(dynamic)
        for (size_t i = 0; i < vs.size(); i++) { int f; long l; std::vector<int> b;
            measure(sv, vs[i], 0, 600, f, l, b); ls[i] = l; fs[i] = f; bs[i] = b; }
        for (size_t i = 0; i < vs.size(); i++) if (ls[i] < bl) { bl = ls[i]; bf = fs[i]; bb = bs[i]; bv = vs[i]; }
        char nb[128]; fb_name(bv, nb);
        printf("%-38s | %4d/1024  %4ld  ", SV2[sv], f1, l1);
        for (int t : b1) printf(" %d", t); printf("%*s", (int)(12 - 4 * (int)b1.size()), "");
        printf("| %4d/1024  %4ld  ", bf, bl);
        for (int t : bb) printf(" %d", t); printf("   %s\n", nb);
    }
    return 0;
}
