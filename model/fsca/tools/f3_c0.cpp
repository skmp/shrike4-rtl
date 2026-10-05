// f3_c0.cpp -- the merged fraction-feedback form makes a prediction nothing else in the model does: at t = 0
// the square path's operand is the ALL-ONES (or 2^w) fraction field, so the product alone must reproduce the
// table value C0_n.  Every t = 0 window pins C0_n exactly, so this is 17 independent yes/no tests with no
// fitting anywhere.  Sweep the field width w, the complement kind, the operand role, the rounding constant and
// all nine sine-field / alignment rules.  ONE rule across all nodes.
#include "f3_fb.h"
#include "e_final.h"
#include <cstdio>
static Fsca DUMP;
int main() {
    if (!load_fsca(DUMP)) { puts("dump load failed"); return 1; } build_tables(); FBF = DUMP; FIN_F = &DUMP;
    // the exact t = 0 windows, straight from the dump
    i64 lo[17], hi[17];
    for (int n = 0; n <= 16; n++) { int a = (n * 1024) & 0xffff; Win w = decode(DUMP.srz[a], DUMP.srn[a]);
        if (w.sign > 0) { lo[n] = w.lo; hi[n] = w.hi; } else { lo[n] = -w.hi; hi[n] = -w.lo; } }
    printf("t=0 windows pin C0 exactly; the documented table is reproduced by [lo,hi] below:\n");
    for (int n = 0; n <= 16; n++) printf("  n=%2d  [%lld,%lld]  documented %lld%s\n", n, (long long)lo[n], (long long)hi[n],
        (long long)FB_C0[n], (FB_C0[n] >= lo[n] && FB_C0[n] <= hi[n]) ? "" : "   <-- MISMATCH");
    puts("\nmerged form: V(t=0) = (arr(field, S_n) + K3) >> u_n  must land in the window for all 17 nodes");
    printf("%-34s %-6s %-5s %-6s %-4s | nodes hit / 17 | which fail\n", "sine rule", "w", "comp", "role", "K3");
    int bestall = -1; int BESTSHOW = 10;
    for (int sv = 0; sv < NSV; sv++) {
        i64 S[17]; int U[17]; for (int n = 0; n <= 16; n++) fin_field(sv, n, S[n], U[n]);
        for (int comp = 0; comp < 2; comp++) for (int role = 0; role < 2; role++) for (int k3 = 0; k3 < 3; k3++)
            for (int w = 26; w <= 40; w++) {
                FbV v{MODE_MERGED, comp, 0, role, k3, w}; int nr = fb_nrows(v); int hit = 0; std::vector<int> bad;
                for (int n = 0; n <= 16; n++) { if (!S[n]) { hit++; continue; }      // node 0: sin = 0, no product
                    i64 V = fb_contrib(v, 0, S[n], U[n], nr, 0);
                    if (V >= lo[n] && V <= hi[n]) hit++; else if (bad.size() < 8) bad.push_back(n); }
                if (hit < BESTSHOW) continue;
                printf("%-34s w=%-4d %-5s role=%-1d k3=%-1d | %2d / 17       |", SVNAME[sv], w, comp ? "1'sc" : "2'sc",
                       role, k3, hit);
                for (int n : bad) printf(" %d", n); puts("");
                if (hit > bestall) bestall = hit;
            }
    }
    printf("\nbest over every merged variant: %d of 17 t=0 windows.  (17 would mean the '+1 effect' really does\n", bestall);
    puts("produce the table value; anything less means the '1*S' term is a SEPARATE adder input, i.e. C0.)");
    return 0;
}
