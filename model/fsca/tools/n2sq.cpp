#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 C0 = 13092290, C1 = 0xc532d6, A2 = 0x1ecd432, A3 = 0xa22fed; double c1 = 0.0305, c3p = 0.0068;
    for (int s : {1, -1}) { printf("node 2 side %+d: square fraction >= 0.99 (needed |T2| vs floor) [c1=%.4f, cubic: sign-mag c3=%.4f]\n", s, c1, c3p);
        for (int a = 1; a <= 511; a++) { int i = (2048 + s * a + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); i64 V = w.sign * w.lo; double e1 = (double)C1 * a / 2048, e2 = (double)A2 * a * a / pow(2, 29), e3 = (double)A3 * (double)a * a * a / pow(2, 40);
            i64 T1 = tz(s * e1 + c1); i64 T3 = -tz(s * e3 + c3p); i64 T2 = V - C0 - T1 - T3; i64 T2m = -T2; double f2 = e2 - floor(e2); if (f2 < 0.99 && !(T2m != (i64)floor(e2))) continue;
            printf("  a=%3d e2=%.4f (frac %.4f) hw |T2|=%lld -> %s | e1 frac %.4f e3 frac %.4f\n", a, e2, f2, (long long)T2m, T2m == (i64)floor(e2) ? "floor" : T2m == (i64)floor(e2) + 1 ? "CARRY" : "OTHER", e1 - floor(e1), e3 - floor(e3)); } }
    return 0; }
