// f3_one.cpp -- full per-offset report for ONE named representation, so a headline number can be checked by
// hand.  usage: f3_one <sv> <mode 0=CUR 1=SEP 2=MERGED> <comp> <tm> <role> <k3> <w> <du> [radius]
#include "f3_fb.h"
#include "e_final.h"
#include <cstdio>
static Fsca DUMP;
static i64 WLO[65536], WHI[65536];
int main(int argc, char** argv) {
    if (!load_fsca(DUMP)) { puts("dump load failed"); return 1; } build_tables(); FBF = DUMP; FIN_F = &DUMP;
    for (int a = 0; a < 65536; a++) { Win w = decode(DUMP.srz[a], DUMP.srn[a]);
        if (w.sign > 0) { WLO[a] = w.lo; WHI[a] = w.hi; } else { WLO[a] = -w.hi; WHI[a] = -w.lo; } }
    int sv = atoi(argv[1]); FbV v{atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7])};
    int du = atoi(argv[8]); i64 RR = (argc > 9 ? atoll(argv[9]) : 600) << (du / 2); int nr = fb_nrows(v);
    i64 S[17]; int U[17]; for (int n = 0; n <= 16; n++) { fin_field(sv, n, S[n], U[n]); U[n] += du; }
    i64 b0[17]; for (int n = 0; n <= 16; n++) b0[n] = fb_base0(v, S[n], U[n], nr);
    char nb[128]; fb_name(v, nb);
    printf("sine rule : %s\nfinal mult: %s\nlatch shift %d, scan radius %lld\n", SVNAME[sv], nb, 20 - du, (long long)RR);
    int feas = 0; long flo = 0; std::vector<int> bad;
    for (int t = -512; t <= 511; t++) {
        i64 dx = (i64)t << 13; i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
        std::vector<NodeCon> cons;
        for (int node = 1; node <= 31; node++) { int flip = node >= 17, n = flip ? 32 - node : node;
            if (!S[n]) continue; i64 s = flip ? -1 : 1;
            i64 T1 = TB[n].C1f ? sm(arr(TB[n].C1f, dx) + K, 23 - TB[n].e1) : 0;
            i64 T3 = TB[n].c3 ? sm(arr(TB[n].c3 << 5, A3) + K, 26) : 0;
            i64 base = s * T1 - s * T3; if (v.mode != MODE_MERGED) base += FB_C0[n];
            int a = (node * 1024 + t) & 0xffff;
            NodeCon c; c.n = n; c.clo = WLO[a] - base; c.chi = WHI[a] - base; c.S = S[n]; c.u = U[n]; c.base0 = b0[n];
            cons.push_back(c); }
        i64 c0 = fb_ideal(t, du); int bv = 1 << 30; i64 jl = 1, jh = 0;
        for (i64 J = std::max((i64)0, c0 - RR); J <= c0 + RR; J++) { int viol = 0;
            for (const NodeCon& q : cons) { i64 g = fb_contrib(v, J, q.S, q.u, nr, q.base0);
                if (g < q.clo || g > q.chi) viol++; }
            if (viol < bv) bv = viol; if (!viol) { if (jl > jh) jl = J; jh = J; } }
        if (!bv) feas++; else { flo += bv; bad.push_back(t); }
        if (t == 17 || t == -17 || t == 342 || t == -342 || t == 376 || t == -376)
            printf("   t=%4d : %s  min-violations %d\n", t, jl <= jh ? "feasible" : "INFEASIBLE", bv),
            (jl <= jh ? printf("            J in [%lld,%lld]\n", (long long)jl, (long long)jh) : 0);
    }
    printf("feasible offsets %d/1024 ; free-J floor %ld ; infeasible:", feas, flo);
    for (size_t i = 0; i < bad.size() && i < 30; i++) printf(" %d", bad[i]);
    if (bad.size() > 30) printf(" ...(%zu)", bad.size()); puts("");
    // and the full model with the published chain
    int bestv = 1 << 30, bestg = 1 << 30, bs1 = 0, bk2 = 0, bro = 0;
    for (int ro = 0; ro < 2; ro++) for (int s1 = 20; s1 <= 26; s1++) { int s2 = 43 - du - s1;
      for (int k2 = 0; k2 < 2; k2++) { i64 K2 = k2 ? (1ll << 19) : 0; int viol = 0, guard = 0;
        for (int t = -512; t <= 511; t++) { i64 dx = (i64)t << 13;
            i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
            i64 I1 = ((ro ? arr(dx, KC) : arr(KC, dx)) + K) >> s1; i64 J = (arr(I1, dx) + K2) >> s2; if (J < 0) J = 0;
            for (int node = 0; node <= 31; node++) { int flip = node >= 17, n = flip ? 32 - node : node; i64 s = flip ? -1 : 1;
                i64 T1 = TB[n].C1f ? sm(arr(TB[n].C1f, dx) + K, 23 - TB[n].e1) : 0;
                i64 T3 = TB[n].c3 ? sm(arr(TB[n].c3 << 5, A3) + K, 26) : 0;
                i64 V = s * T1 - s * T3 + fb_contrib(v, J, S[n], U[n], nr, b0[n]);
                if (v.mode != MODE_MERGED || !S[n]) V += FB_C0[n];
                int a = (node * 1024 + t) & 0xffff;
                if (V < WLO[a] || V > WHI[a]) { viol++; if (n == 0 || n == 1 || n == 2 || n == 16) guard++; } } }
        if (guard < bestg || (guard == bestg && viol < bestv)) { bestv = viol; bestg = guard; bs1 = s1; bk2 = k2; bro = ro; } } }
    printf("full model (published chain, best split): %d window violations, guard nodes 0/1/2/16: %d  [s1=%d s2=%d K2=%d %s]\n",
           bestv, bestg, bs1, 43 - du - bs1, bk2, bro ? "arr(dx,KC)" : "arr(KC,dx)");
    return 0;
}
