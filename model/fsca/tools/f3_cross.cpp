// f3_cross.cpp -- the full cross the user asked for: the FEEDBACK INPUT (fraction field, integer bits cleared)
// x OPERAND PLACEMENT (which side is recoded) x COMPLEMENT ENCODING (two's / one's) x the rounding constant
// x the TRUNCATION PLACEMENT x the SINE-FIELD / ALIGNMENT rule (all nine of e_final.h).
//
// The question: does any of it make t = +-17 feasible at the COARSE latch (shift 20, u = 30 + k), where the
// published rule fails, while keeping D(342) = J(-342) - J(342) in its window?
// If yes, the "the latch must be two or three bits finer" conclusion dissolves.
//
// Every variant regenerates its own latch dataset.  ONE rule across all nodes in every case.
#include "f3_fb.h"
#include "e_final.h"
#include <cstdio>
#include <string>
static Fsca DUMP;
int main(int argc, char** argv) {
    if (!load_fsca(DUMP)) { puts("dump load failed"); return 1; } build_tables();
    FBF = DUMP; FIN_F = &DUMP;
    i64 R = argc > 1 ? atoll(argv[1]) : 600;
    const int OFF[6] = { 17, -17, 342, -342, 376, -376 };
    for (int du = 0; du <= 2; du += 2) {
        std::vector<FbV> vs;
        vs.push_back(FbV{MODE_CUR, 0, 0, 0, 1, 0});
        vs.push_back(FbV{MODE_CUR, 0, 0, 1, 1, 0});
        vs.push_back(FbV{MODE_CUR, 0, 0, 0, 0, 0});
        vs.push_back(FbV{MODE_CUR, 0, 0, 0, 2, 0});
        for (int comp = 0; comp < 2; comp++) for (int tm = 0; tm < 3; tm++) for (int role = 0; role < 2; role++)
            for (int k3 = 0; k3 < 3; k3++) for (int w = 23 + du; w <= 38; w++) {
                FbV v{MODE_SEP, comp, tm, role, k3, w}; if (fb_valid(v, du)) vs.push_back(v); }
        for (int comp = 0; comp < 2; comp++) for (int role = 0; role < 2; role++) for (int k3 = 0; k3 < 3; k3++) {
            FbV v{MODE_MERGED, comp, 0, role, k3, 33 + du}; if (fb_valid(v, du)) vs.push_back(v); }
        int NV = (int)vs.size(), NT = NSV * NV;
        printf("\n============ latch shift %d (u = <sine rule> + %d) : %d sine rules x %d feedback variants = %d ============\n",
               20 - du, du, NSV, NV, NT);
        std::vector<std::string> hit; std::vector<int> ok17(NT, 0), ok342(NT, 0);
        #pragma omp parallel for schedule(dynamic)
        for (int idx = 0; idx < NT; idx++) {
            int sv = idx / NV, i = idx % NV; const FbV& v = vs[i];
            i64 S[17]; int U[17]; for (int n = 0; n <= 16; n++) fin_field(sv, n, S[n], U[n]);
            // install this sine rule for this thread's evaluation (thread-local copies)
            i64 lo[6], hi[6]; long cnt[6]; i64 RR = R << (du / 2);
            // local re-implementation of fb_feasible with the variant field (keeps the header single-threaded-safe)
            for (int j = 0; j < 6; j++) {
                int t = OFF[j]; std::vector<NodeCon> cons; i64 dx = (i64)t << 13; int nr = fb_nrows(v);
                i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
                for (int node = 1; node <= 31; node++) { int flip = node >= 17, n = flip ? 32 - node : node;
                    if (!S[n]) continue; i64 s = flip ? -1 : 1;
                    i64 T1 = TB[n].C1f ? sm(arr(TB[n].C1f, dx) + K, 23 - TB[n].e1) : 0;
                    i64 T3 = TB[n].c3 ? sm(arr(TB[n].c3 << 5, A3) + K, 26) : 0;
                    i64 base = s * T1 - s * T3; if (v.mode != MODE_MERGED) base += FB_C0[n];
                    int a = (node * 1024 + t) & 0xffff; i64 l, h; { Win w2 = decode(DUMP.srz[a], DUMP.srn[a]);
                        if (w2.sign > 0) { l = w2.lo; h = w2.hi; } else { l = -w2.hi; h = -w2.lo; } }
                    NodeCon c; c.n = n; c.clo = l - base; c.chi = h - base; c.S = S[n]; c.u = U[n] + du;
                    c.base0 = fb_base0(v, c.S, c.u, nr); cons.push_back(c); }
                std::sort(cons.begin(), cons.end(), [](const NodeCon& a, const NodeCon& b){ return a.chi-a.clo < b.chi-b.clo; });
                i64 c0 = fb_ideal(t, du), a0 = std::max((i64)0, c0 - RR), b0 = c0 + RR;
                long n = 0; i64 jl = 1, jh = 0;
                for (i64 J = a0; J <= b0; J++) { bool ok = true;
                    for (const NodeCon& q : cons) { i64 g = fb_contrib(v, J, q.S, q.u, nr, q.base0);
                        if (g < q.clo || g > q.chi) { ok = false; break; } }
                    if (ok) { if (jl > jh) jl = J; jh = J; n++; } }
                cnt[j] = n; lo[j] = jl; hi[j] = jh;
            }
            ok17[idx] = (cnt[0] > 0 && cnt[1] > 0); ok342[idx] = (cnt[2] > 0 && cnt[3] > 0);
            if (ok17[idx]) { char nb[128]; fb_name(v, nb); char b[512];
                char dr[48]; if (cnt[2] && cnt[3]) snprintf(dr, 48, "D in [%lld,%lld]", (long long)(lo[3]-hi[2]), (long long)(hi[3]-lo[2]));
                else snprintf(dr, 48, "t=342 INFEASIBLE");
                snprintf(b, 512, "  sv%d %-34s | %-44s | %s | 376 %s%s", sv, SVNAME[sv], nb, dr,
                         cnt[4] ? "OK" : "--", cnt[5] ? "OK" : "--");
                #pragma omp critical
                hit.push_back(b); }
        }
        long n17 = 0; for (int i = 0; i < NT; i++) n17 += ok17[i];
        printf("t=+17 AND t=-17 both feasible: %ld of %d variants\n", n17, NT);
        std::sort(hit.begin(), hit.end());
        for (size_t i = 0; i < hit.size() && i < 60; i++) printf("%s\n", hit[i].c_str());
        if (hit.size() > 60) printf("  ... %zu more\n", hit.size() - 60);
    }
    return 0;
}
