#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 C0 = 13092290, C1 = 0xc532d6, C2 = 0x1ecd432, C3 = 0xa22fed; double c1 = 0.031, c2 = 0.0034;
    for (int s : {1, -1}) { printf("node 2 side %+d: samples with cubic fraction >= 0.985 (needed cubic magnitude vs floor):\n", s); for (int a = 1; a <= 511; a++) { int i = (2048 + s * a + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); i64 V = w.sign * w.lo; double e1 = (double)C1 * a / 2048, e2 = (double)C2 * a * a / pow(2, 29), e3 = (double)C3 * (double)a * a * a / pow(2, 40); double f3 = e3 - floor(e3); if (f3 < 0.985) continue;
        i64 T1 = tz(s * e1 + c1), T2 = tz(-e2 - c2); i64 T3 = V - C0 - T1 - T2; i64 T3m = s > 0 ? -T3 : T3; printf("  a=%3d e3=%.4f (frac %.4f) hw cubic magnitude %lld -> %s  [e1 frac %.4f e2 frac %.4f]\n", a, e3, f3, (long long)T3m, T3m == (i64)floor(e3) ? "floor" : T3m == (i64)floor(e3) + 1 ? "CARRY" : "other", e1 - floor(e1), e2 - floor(e2)); } }
    return 0; }
