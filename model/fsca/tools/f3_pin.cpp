// f3_pin.cpp -- the question the user asked, answered directly.
//
// "Does modelling the feedback path explicitly (the latch holds the FRACTION of a negative quantity with the
//  integer bits cleared, so the operand is 2^w - y) -- crossed with operand placement, complement encoding and
//  the rounding constant -- make t = +-17 feasible at the COARSE latch (shift 20, u2 = 30 + k) while keeping
//  D(342) = J(-342) - J(342) inside its window?"
//
// For every variant, the latch DATASET is regenerated: the pinned offsets are a property of the final multiply,
// so they must be recomputed per variant (the point of e_final.h).  Bounded scan of J around the ideal
// KC*t^2/2^(17-du), radius R -- the residual deviations are < 25 latch units everywhere, so R = 600 (coarse)
// is ~25x margin.  One rule across all nodes: w, comp, role, k3, truncation placement are global; the only
// per-node quantities are the stored S_n and the published alignment u_n = 30 + k_n (+du).
#include "f3_fb.h"
#include <cstdio>
#include <string>
int main(int argc, char** argv) {
    if (!load_fsca(FBF)) { puts("dump load failed"); return 1; } build_tables();
    i64 R = argc > 1 ? atoll(argv[1]) : 600;
    const int OFF[6] = { 17, -17, 342, -342, 376, -376 };
    for (int du = 0; du <= 2; du += 2) {
        printf("\n================ latch shift %d  (u2 = 30 + k + %d)  scan radius %lld ================\n",
               20 - du, du, (long long)(R << (du / 2)));
        printf("%-52s | t=17    t=-17   | t=342 window        t=-342 window       D range        | 376 -376\n", "final-multiply variant (one rule across all nodes)");
        std::vector<FbV> vs;
        vs.push_back(FbV{MODE_CUR, 0, 0, 0, 1, 0});                      // the published rule, as control
        vs.push_back(FbV{MODE_CUR, 0, 0, 1, 1, 0});
        for (int comp = 0; comp < 2; comp++) for (int tm = 0; tm < 3; tm++) for (int role = 0; role < 2; role++)
            for (int k3 = 0; k3 < 3; k3++) for (int w = 23 + du; w <= 38; w++) {
                FbV v{MODE_SEP, comp, tm, role, k3, w}; if (fb_valid(v, du)) vs.push_back(v); }
        for (int comp = 0; comp < 2; comp++) for (int role = 0; role < 2; role++) for (int k3 = 0; k3 < 3; k3++) {
            FbV v{MODE_MERGED, comp, 0, role, k3, 33 + du}; if (fb_valid(v, du)) vs.push_back(v); }
        int N = (int)vs.size();
        std::vector<std::string> lines(N); std::vector<int> ok17f(N, 0);
        #pragma omp parallel for schedule(dynamic)
        for (int i = 0; i < N; i++) {
            const FbV& v = vs[i]; i64 lo[6], hi[6]; long cnt[6]; i64 RR = R << (du / 2);
            for (int j = 0; j < 6; j++) cnt[j] = fb_feasible(v, OFF[j], du, RR, lo[j], hi[j]);
            ok17f[i] = (cnt[0] > 0 && cnt[1] > 0);
            char nb[128]; fb_name(v, nb); char b[512];
            char d17[32], dm17[32], d376[16], dm376[16];
            snprintf(d17, 32, "%s", cnt[0] ? "OK" : "--"); snprintf(dm17, 32, "%s", cnt[1] ? "OK" : "--");
            snprintf(d376, 16, "%s", cnt[4] ? "OK" : "--"); snprintf(dm376, 16, "%s", cnt[5] ? "OK" : "--");
            char dr[40];
            if (cnt[2] && cnt[3]) snprintf(dr, 40, "[%lld,%lld]", (long long)(lo[3] - hi[2]), (long long)(hi[3] - lo[2]));
            else snprintf(dr, 40, "n/a");
            char w2[48], w3[48];
            if (cnt[2]) snprintf(w2, 48, "[%lld,%lld]", (long long)lo[2], (long long)hi[2]); else snprintf(w2, 48, "INFEASIBLE");
            if (cnt[3]) snprintf(w3, 48, "[%lld,%lld]", (long long)lo[3], (long long)hi[3]); else snprintf(w3, 48, "INFEASIBLE");
            snprintf(b, 512, "%-52s | %-6s  %-6s | %-19s %-19s %-14s | %-3s %-3s", nb, d17, dm17, w2, w3, dr, d376, dm376);
            lines[i] = b;
        }
        // print the control first, then anything that makes t=+-17 feasible, then a summary
        int n17 = 0, nboth = 0;
        for (int i = 0; i < N; i++) {
            bool ctrl = vs[i].mode == MODE_CUR;
            bool ok17 = ok17f[i];
            if (ok17) n17++;
            if (ctrl || ok17) printf("%s%s\n", lines[i].c_str(), ctrl ? "   <-- control" : "   <== t=+-17 FEASIBLE");
        }
        printf("-- %d of %d variants make BOTH t=+17 and t=-17 feasible at this latch scale\n", n17, N);
        if (!n17) {
            puts("-- printing the three closest variants by t=342/-342 behaviour for the record:");
            for (int i = 0; i < N && i < 3; i++) printf("   %s\n", lines[i].c_str());
        }
        (void)nboth;
    }
    return 0;
}
