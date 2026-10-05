#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { int n = 2; i64 C0 = 13092290; Fsca F; if (!load_fsca(F)) return 1; double th = 2 * M_PI * n / 64; int s1 = 1, s2 = 1;
    struct P { int s; i64 m1, m2, m3; double c1, c2, c3; } ps[2] = { { 1, 0xc532d6, 0x1ecd44a, 0xa22fcd, 0.03809, 0.00293, -0.00684 }, { -1, 0xc532d6, 0x1ecd436, 0xa22fed, 0.03809, -0.00293, -0.00391 } };
    int near = 0, tot = 0;
    for (auto& p : ps) { int s = p.s; printf("node 2 side %+d misses:\n", s); for (int a = 1; a <= 511; a++) { int i = (n * 1024 + s * a + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; i64 V = w.sign * w.lo;
        double e1 = (double)p.m1 * a / 2048, e2 = (double)p.m2 * a * a / pow(2, 29), e3 = (double)p.m3 * a * (double)a * a / pow(2, 40); i64 T1 = tz(s * s1 * e1 + p.c1), T2 = tz(-s2 * e2 + p.c2), T3 = tz(-s * s1 * e3 + p.c3); i64 W = C0 + T1 + T2 + T3;
        double f1 = e1 - floor(e1), f2 = e2 - floor(e2), f3 = e3 - floor(e3); tot++; if (fabs(f1 - round(f1)) < 0.002 || fabs(f2 - round(f2)) < 0.002 || fabs(f3 - round(f3)) < 0.002) near++;
        if (W != V) printf("  a=%3d: V %lld model %lld (%+lld) | e1 %.4f (frac %.4f) e2 %.4f (frac %.4f) e3 %.4f (frac %.4f)\n", a, (long long)V, (long long)W, (long long)(V - W), e1, f1, e2, f2, e3, f3); } }
    printf("samples with any product within 0.002 of an integer: %d of %d\n", near, tot); return 0; }
