#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 C0 = 13092290, C1 = 0xc532d6, C2 = 0x1ecd432, C3 = 0xa22fed; double c1 = 0.031, c3 = 0.0058;
    // for each + side sample: needed T2 -> interval for c2 (T2 = -floor(e2 + c2))
    double lo = -1e9, hi = 1e9; int alo = 0, ahi = 0; double flo = 0, fhi = 0;
    for (int a = 1; a <= 511; a++) { Win w = decode(F.srz[2048 + a], F.srn[2048 + a]); i64 V = w.sign * w.lo; double e1 = (double)C1 * a / 2048, e2 = (double)C2 * a * a / pow(2, 29), e3 = (double)C3 * (double)a * a * a / pow(2, 40); i64 T1 = tz(e1 + c1), T3 = tz(-e3 - c3); i64 T2 = V - C0 - T1 - T3; i64 T2m = -T2;
        double l = T2m - e2, h = l + 1; if (l > lo) { lo = l; alo = a; flo = e2 - floor(e2); } if (h < hi) { hi = h; ahi = a; fhi = e2 - floor(e2); } }
    printf("node 2 + side, c2 interval given c1=%.3f c3=%.4f: [%.4f, %.4f) %s ; binding a=%d (e2 frac %.4f) and a=%d (e2 frac %.4f)\n", c1, c3, lo, hi, lo < hi ? "ok" : "INFEASIBLE", alo, flo, ahi, fhi);
    for (int a : {alo, ahi}) { Win w = decode(F.srz[2048 + a], F.srn[2048 + a]); double e1 = (double)C1 * a / 2048, e2 = (double)C2 * a * a / pow(2, 29), e3 = (double)C3 * (double)a * a * a / pow(2, 40); printf("  a=%d: V=%lld e1=%.4f e2=%.4f e3=%.4f\n", a, (long long)(w.sign * w.lo), e1, e2, e3); }
    return 0; }
