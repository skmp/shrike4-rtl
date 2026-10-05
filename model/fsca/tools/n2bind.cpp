#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 C0 = 13092290, C1 = 0xc532d6; 
    for (double c3 : {0.0039, 0.0045, 0.0068}) for (i64 C2 : {(i64)0x1ecd42a, (i64)0x1ecd44a, (i64)0x1ecd436}) for (double c2 : {-0.003, 0.0, 0.003}) { double lo = -1e9, hi = 1e9; int alo = 0, ahi = 0; double flo = 0, fhi = 0;
        for (int a = 1; a <= 511; a++) { int i = 2048 + a; Win w = decode(F.srz[i], F.srn[i]); i64 V = w.sign * w.lo; double e1 = (double)C1 * a / 2048, e2 = (double)C2 * a * a / pow(2, 29), e3 = (double)0xa22fed * (double)a * a * a / pow(2, 40);
            i64 T2 = tz(-e2 - c2), T3 = tz(-e3 - c3); i64 T1 = V - C0 - T2 - T3; double l = T1 - e1, h = l + 1; if (l > lo) { lo = l; alo = a; flo = e1 - floor(e1); } if (h < hi) { hi = h; ahi = a; fhi = e1 - floor(e1); } }
        printf("c3 %.4f C2 %llx c2 %+.3f: c1 in [%.4f, %.4f) %s  binding: a=%d (e1 frac %.4f needs carry) vs a=%d (frac %.4f no carry)\n", c3, (long long)C2, c2, lo, hi, lo < hi ? "ok" : "INFEASIBLE", alo, flo, ahi, fhi); }
    return 0; }
