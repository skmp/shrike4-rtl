#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static void node(int n, i64 C0, i64 m1, i64 m2, i64 m3, double c1, double c2, double c3, const int* as, int na) { double k = 2 * M_PI / 65536, th = 2 * M_PI * n / 64; int s1 = cos(th) >= 0 ? 1 : -1, s2 = sin(th) >= 0 ? 1 : -1; Fsca F; load_fsca(F);
    printf("node %d:\n", n); for (int q = 0; q < na; q++) for (int a = as[q] - 2; a <= as[q] + 2; a++) for (int s : {1, -1}) { int i = (n * 1024 + s * a + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); i64 V = w.sign * w.lo;
        double e1 = (double)m1 * a / 2048, e2 = (double)m2 * a * a / pow(2, 29), e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 T1 = tz(s * s1 * e1 + c1), T2 = tz(-s2 * e2 + c2), T3 = tz(-s * s1 * e3 + c3); i64 W = C0 + T1 + T2 + T3;
        printf("  a=%3d (0x%03x) s=%+d: V=%9lld model=%9lld diff=%+lld | e1=%.4f e2=%.4f e3=%.4f | T1 %lld T2 %lld T3 %lld\n", a, a, s, (long long)V, (long long)W, (long long)(V - W), e1, e2, e3, (long long)T1, (long long)T2, (long long)T3); } }
int main() { int a1[] = { 207, 479 }; node(1, 6577819, 0xc81800, 0xf79b60, 0xa49008, 0.0395, 0.0, 0.0045, a1, 2); int a2[] = { 208, 277, 308 }; node(2, 13092290, 0xc532d6, 0x1ecd42a, 0xa22fed, 0.0395, 0.0, 0.0045, a2, 3); return 0; }
