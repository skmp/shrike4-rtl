#include "fsca_common.h"
#include <cmath>
#include <map>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; std::map<i64,int> h; int nx = 0;
    for (int t = -512; t <= 511; t++) { Win a = decode(F.srz[31744 + t], F.srn[31744 + t]), b = decode(F.srz[1024 - t], F.srn[1024 - t]); if (!a.exact || !b.exact) { nx++; continue; } h[(i64)a.lo - (i64)b.lo]++; }
    printf("node 31 vs mirrored node 1: non-exact %d; diff histogram:", nx); for (auto& kv : h) printf(" %lld:%d", (long long)kv.first, kv.second); printf("\n");
    // node 31 with node 1's coefficients mirrored, under 4 constant conventions
    i64 C0 = 6577819, C1 = -13113344, C2 = -16227180, C3 = 10784782;
    for (int conv = 0; conv < 4; conv++) { int bad = 0; for (int t = -512; t <= 511; t++) { int i = 31744 + t; Win w = decode(F.srz[i], F.srn[i]); int a = t < 0 ? -t : t, s = t < 0 ? -1 : 1; double e1 = (double)C1 * a / 2048, e2 = (double)C2 * a * a / pow(2, 29), e3 = (double)C3 * a * (double)a * a / pow(2, 40);
        double p1 = s * e1, p3 = s * e3; i64 T1 = (conv & 1) ? (p1 >= 0 ? (i64)floor(p1 + 0.035) : -(i64)floor(-p1 + 0.035)) : tz(p1 + 0.035); i64 T3 = (conv & 2) ? (p3 >= 0 ? (i64)floor(p3 + 0.0045) : -(i64)floor(-p3 + 0.0045)) : tz(p3 - 0.0045);
        i64 V = C0 + T1 + tz(e2) + T3; i64 m = V < 0 ? -V : V; if (m < (i64)w.lo || m > (i64)w.hi) bad++; }
        printf("node 31 conv %d (linear const on %s, cubic const on %s): %d misses\n", conv, (conv & 1) ? "magnitude" : "signed", (conv & 2) ? "magnitude" : "signed", bad); }
    return 0; }
