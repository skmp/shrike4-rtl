// Two's-complement floor form: V = C0' + floor(S1+f1) + floor(S2+f2) + floor(S3+f3), f3 = 1 - c3, f2 = |c2|, C0' = C0 + [has square]. Compare with the tz form on exact samples of nodes 0..2.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; load_fsca(F); double c1[3] = { 0.0395, 0.5, 0.0308 }, c2[3] = { 0, 0, -0.004 }, c3[3] = { 0.0045, 0.002, 0.0045 };
    for (int n = 0; n < 3; n++) { const i64* C = FSCA_TAB[n]; int mt = 0, mf = 0, diff = 0, N = 0;
        for (int t = -512; t <= 511; t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; N++; i64 V = (i64)w.sign * w.lo;
            double S1 = (double)C[1] * t / 2048, S2 = (double)C[2] * t * (double)t / 536870912.0, S3 = (double)C[3] * t * (double)t * t / 1099511627776.0;
            i64 Vt = C[0] + tz(S1 + c1[n]) + (C[2] ? -tz(fabs(S2) + c2[n]) : 0) - tz(fabs(S3) * (t < 0 ? -1 : 1) + c3[n]) * 1; // tz form: T3 = -tz(|C3| t^3 + c3) (positive coefficient, subtract)
            i64 Vf = C[0] + (C[2] ? 1 : 0) + (i64)floor(S1 + c1[n]) + (C[2] ? (i64)floor(S2 + fabs(c2[n])) : 0) + (i64)floor(S3 + 1 - c3[n]);
            if (Vt != V) mt++; if (Vf != V) mf++; if (Vt != Vf) diff++; }
        printf("node %d: exact samples %d, tz-form misses %d, floor-form misses %d, forms differ on %d\n", n, N, mt, mf, diff); }
    return 0; }
