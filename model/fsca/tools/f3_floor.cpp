// f3_floor.cpp -- THE decisive test of the user's question.
//
// At the COARSE latch (shift 20, u = 30 + k) the published final multiply has free-J floor 2, and the only two
// failing offsets are t = +-17.  At shift 18 the floor is 0.  That gap is the whole basis for the "the latch is
// two or three bits finer than u2 = 30 + k implies" conclusion.  If ANY feedback / placement / complement /
// sine-rule representation drives the coarse floor to 0, that conclusion dissolves.
//
// For every variant, regenerate the dataset and measure:
//   * how many of the 1024 offsets admit ANY shared integer latch J at all
//   * the free-J floor: sum over t of the minimum window violations achievable with a free J(t)
//   * which offsets fail
// Bounded scan of J, radius R about KC*t^2/2^(17-du), centre-out so a feasible offset resolves in a few steps.
#include "f3_fb.h"
#include "e_final.h"
#include <cstdio>
#include <string>
static Fsca DUMP;
static i64 WLO[65536], WHI[65536];
struct Out { int sv; FbV v; int feas; long floor_; int nbad; int bad[8]; };
int main(int argc, char** argv) {
    if (!load_fsca(DUMP)) { puts("dump load failed"); return 1; } build_tables(); FBF = DUMP; FIN_F = &DUMP;
    for (int a = 0; a < 65536; a++) { Win w = decode(DUMP.srz[a], DUMP.srn[a]);
        if (w.sign > 0) { WLO[a] = w.lo; WHI[a] = w.hi; } else { WLO[a] = -w.hi; WHI[a] = -w.lo; } }
    i64 R = argc > 1 ? atoll(argv[1]) : 600;
    const int ABORT = argc > 2 ? atoi(argv[2]) : 24;   // give up on a variant after this many infeasible offsets
    for (int du = 0; du <= 2; du += 2) {
        std::vector<FbV> vs;
        for (int role = 0; role < 2; role++) for (int k3 = 0; k3 < 3; k3++) vs.push_back(FbV{MODE_CUR,0,0,role,k3,0});
        for (int comp = 0; comp < 2; comp++) for (int tm = 0; tm < 3; tm++) for (int role = 0; role < 2; role++)
            for (int k3 = 0; k3 < 3; k3++) for (int w = 23 + du; w <= 38; w++) {
                FbV v{MODE_SEP, comp, tm, role, k3, w}; if (fb_valid(v, du)) vs.push_back(v); }
        for (int comp = 0; comp < 2; comp++) for (int role = 0; role < 2; role++) for (int k3 = 0; k3 < 3; k3++) {
            FbV v{MODE_MERGED, comp, 0, role, k3, 33 + du}; if (fb_valid(v, du)) vs.push_back(v); }
        int NV = (int)vs.size(), NT = NSV * NV; i64 RR = R << (du / 2);
        printf("\n===== FREE-J FLOOR, latch shift %d : %d sine rules x %d variants, scan radius %lld =====\n",
               20 - du, NSV, NV, (long long)RR);
        std::vector<Out> res(NT);
        #pragma omp parallel for schedule(dynamic)
        for (int idx = 0; idx < NT; idx++) {
            int sv = idx / NV, i = idx % NV; const FbV& v = vs[i]; int nr = fb_nrows(v);
            i64 S[17]; int U[17]; for (int n = 0; n <= 16; n++) { fin_field(sv, n, S[n], U[n]); U[n] += du; }
            i64 b0[17]; for (int n = 0; n <= 16; n++) b0[n] = fb_base0(v, S[n], U[n], nr);
            Out o; o.sv = sv; o.v = v; o.feas = 0; o.floor_ = 0; o.nbad = 0;
            std::vector<NodeCon> cons;
            for (int t = -512; t <= 511; t++) {
                i64 dx = (i64)t << 13; i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
                cons.clear();
                for (int node = 1; node <= 31; node++) { int flip = node >= 17, n = flip ? 32 - node : node;
                    if (!S[n]) continue; i64 s = flip ? -1 : 1;
                    i64 T1 = TB[n].C1f ? sm(arr(TB[n].C1f, dx) + K, 23 - TB[n].e1) : 0;
                    i64 T3 = TB[n].c3 ? sm(arr(TB[n].c3 << 5, A3) + K, 26) : 0;
                    i64 base = s * T1 - s * T3; if (v.mode != MODE_MERGED) base += FB_C0[n];
                    int a = (node * 1024 + t) & 0xffff;
                    NodeCon c; c.n = n; c.clo = WLO[a] - base; c.chi = WHI[a] - base; c.S = S[n]; c.u = U[n];
                    c.base0 = b0[n]; cons.push_back(c); }
                std::sort(cons.begin(), cons.end(), [](const NodeCon& a, const NodeCon& b){ return a.chi-a.clo < b.chi-b.clo; });
                i64 c0 = fb_ideal(t, du); bool found = false;
                for (i64 d = 0; d <= RR && !found; d++) for (int sgn = 0; sgn < 2 && !found; sgn++) {
                    if (d == 0 && sgn) continue; i64 J = c0 + (sgn ? -d : d); if (J < 0) continue; bool ok = true;
                    for (const NodeCon& q : cons) { i64 g = fb_contrib(v, J, q.S, q.u, nr, q.base0);
                        if (g < q.clo || g > q.chi) { ok = false; break; } }
                    if (ok) found = true; }
                if (found) { o.feas++; continue; }
                // a variant with more than ABORT infeasible offsets is already far worse than the published
                // rule (floor 2), so stop scoring it: the question is only whether anything reaches 0.
                if (o.nbad >= ABORT) { o.nbad++; o.floor_ = 999999; break; }
                int bv = 1 << 30;
                for (i64 J = std::max((i64)0, c0 - RR); J <= c0 + RR; J++) { int viol = 0;
                    for (const NodeCon& q : cons) { i64 g = fb_contrib(v, J, q.S, q.u, nr, q.base0);
                        if (g < q.clo || g > q.chi) { if (++viol >= bv) break; } }
                    if (viol < bv) bv = viol; }
                o.floor_ += bv; if (o.nbad < 8) o.bad[o.nbad] = t; o.nbad++;
            }
            res[idx] = o;
        }
        std::vector<int> ord(NT); for (int i = 0; i < NT; i++) ord[i] = i;
        std::sort(ord.begin(), ord.end(), [&](int a, int b) {
            if (res[a].floor_ != res[b].floor_) return res[a].floor_ < res[b].floor_;
            return res[a].feas > res[b].feas; });
        printf("%-34s %-44s %-9s %-7s %s\n", "sine rule", "final multiply", "feasible", "floor", "infeasible offsets");
        int shown = 0;
        for (int r = 0; r < NT && shown < 20; r++) { const Out& o = res[ord[r]]; char nb[128]; fb_name(o.v, nb);
            printf("%-34s %-44s %4d/1024 %-7ld", SVNAME[o.sv], nb, o.feas, o.floor_);
            for (int j = 0; j < o.nbad && j < 8; j++) printf(" %d", o.bad[j]); if (o.nbad > 8) printf(" ...(%d)", o.nbad);
            puts(""); shown++; }
        // the published rule, for reference
        for (int i = 0; i < NT; i++) { const Out& o = res[i];
            if (o.sv == 0 && o.v.mode == MODE_CUR && o.v.role == 0 && o.v.k3 == 1) { char nb[128]; fb_name(o.v, nb);
                printf("REF %-30s %-44s %4d/1024 %-7ld", SVNAME[o.sv], nb, o.feas, o.floor_);
                for (int j = 0; j < o.nbad && j < 8; j++) printf(" %d", o.bad[j]); puts(""); } }
        long best = res[ord[0]].floor_;
        printf(">>> best free-J floor at latch shift %d over %d representations: %ld\n", 20 - du, NT, best);
    }
    return 0;
}
