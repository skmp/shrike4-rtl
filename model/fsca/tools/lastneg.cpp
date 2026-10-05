// rule test: signed products P1,P2,P3; T1 = tz(P1 + c1); "last negative" rule: the last negative product among (P1,P2,P3) gets -kappa, others 0
// baseline: T2 = tz(P2 + s2*c2), T3 = tz(P3 + s3*c3) with fixed signed constants (sign of coefficient), c2 = 0, c3 = 0.0045
#include "fsca_common.h"
#include <cmath>
#include <vector>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536;
    for (int n : {0, 1, 2, 30, 31, 33, 34, 62, 63}) { double th = 2 * M_PI * n / 64; int m = n % 32; if (m > 16) m = 32 - m;   // magnitude coefficients from the fitted tables of nodes 0,1,2
        i64 A1[3] = { 0xc90fda, 0xc81800, 0xc532d6 }, A2[3] = { 0, 0xf79b60, 0x1ecd432 }, A3[3] = { 0xa56000, 0xa49008, 0xa22fed }; i64 C0m[3] = { 0, 6577819, 13092290 };
        i64 C0 = (sin(th) >= 0 ? 1 : -1) * C0m[m]; i64 C1 = (cos(th) >= 0 ? 1 : -1) * A1[m], C2 = -(sin(th) >= 0 ? 1 : -1) * A2[m], C3 = -(cos(th) >= 0 ? 1 : -1) * A3[m];
        std::vector<int> ts; std::vector<i64> Vs; for (int t = -512; t <= 511; t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; ts.push_back(t); Vs.push_back(w.sign * w.lo); }
        int bestB = 1 << 30, bestL = 1 << 30; double bc1B = 0, bc1L = 0, bkL = 0;
        for (int i1 = 40; i1 <= 90; i1++) { double c1 = i1 / 2048.0; int mB = 0; for (size_t j = 0; j < ts.size() && mB <= bestB; j++) { i64 t = ts[j]; double P1 = (double)C1 * t / 2048, P2 = (double)C2 * t * (double)t / pow(2, 29), P3 = (double)C3 * t * (double)t * t / pow(2, 40);
                i64 W = C0 + tz(P1 + (C1 < 0 ? -c1 : c1)) + tz(P2) + tz(P3 + (C3 < 0 ? -0.0045 : 0.0045)); if (W != Vs[j]) mB++; } if (mB < bestB) { bestB = mB; bc1B = c1; }
            for (int ik = 4; ik <= 16; ik++) { double kap = ik / 2048.0; int mL = 0; for (size_t j = 0; j < ts.size() && mL <= bestL; j++) { i64 t = ts[j]; double P1 = (double)C1 * t / 2048, P2 = (double)C2 * t * (double)t / pow(2, 29), P3 = (double)C3 * t * (double)t * t / pow(2, 40);
                    double d2 = 0, d3 = 0, d1 = 0; if (P3 < 0) d3 = -kap; else if (P2 < 0) d2 = -kap; else if (P1 < 0) d1 = -kap;
                    i64 W = C0 + tz(P1 + (C1 < 0 ? -c1 : c1) + d1) + tz(P2 + d2) + tz(P3 + d3); if (W != Vs[j]) mL++; } if (mL < bestL) { bestL = mL; bc1L = c1; bkL = kap; } } }
        printf("node %2d: baseline signed-const %3d (c1 %.4f) | last-negative rule %3d (c1 %.4f kappa %.4f)\n", n, bestB, bc1B, bestL, bc1L, bkL); }
    return 0; }
