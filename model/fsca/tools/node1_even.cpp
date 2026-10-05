#include "fsca_common.h"
#include <cmath>
#include <vector>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main(int argc, char** argv) { int n = atoi(argv[1]); i64 C0 = atoll(argv[2]); i64 m1 = strtoll(argv[3], 0, 0), m3 = strtoll(argv[4], 0, 0); double c1 = 0.0395, c3 = 0.0045; Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536, th = 2 * M_PI * n / 64;
    std::vector<int> as; std::vector<i64> Ev; for (int a = 1; a <= 511; a++) { int ip = (n * 1024 + a) % 65536, im = (n * 1024 - a + 65536) % 65536; Win wp = decode(F.srz[ip], F.srn[ip]), wm = decode(F.srz[im], F.srn[im]); if (!wp.exact || !wm.exact) continue; as.push_back(a); Ev.push_back(wp.sign * wp.lo + wm.sign * wm.lo); } int N = as.size();
    double C2t = fabs(sin(th)) * k * k / 2 * pow(2, 55); int s1 = cos(th) >= 0 ? 1 : -1, s2 = sin(th) >= 0 ? 1 : -1;
    int best = 1 << 30; i64 bm2 = 0; double bc2 = 0; int nz = 0; i64 m2lo = 1ll << 60, m2hi = -1; double c2lo = 9, c2hi = -9;
    for (i64 m2 = (i64)(C2t * 0.999); m2 <= (i64)(C2t * 1.001); m2 += 1) for (int ic = -128; ic <= 128; ic += 2) { double c2 = ic / 1024.0; int mism = 0;
        for (int j = 0; j < N && mism <= best; j++) { int a = as[j]; double e1 = (double)m1 * a / 2048, e2 = (double)m2 * a * a / pow(2, 29), e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 E = 2 * C0 + tz(s1 * e1 + c1) + tz(-s1 * e1 + c1) + 2 * tz(-s2 * e2 + c2) + tz(-s1 * e3 + c3) + tz(s1 * e3 + c3); if (E != Ev[j]) mism++; }
        if (mism < best) { best = mism; bm2 = m2; bc2 = c2; } if (mism == 0) { nz++; if (m2 < m2lo) m2lo = m2; if (m2 > m2hi) m2hi = m2; if (c2 < c2lo) c2lo = c2; if (c2 > c2hi) c2hi = c2; } }
    printf("node %d EVEN (C0=%lld pinned): best %d C2 0x%07llx (true %.1f, x%.6f) c2 %.4f | exact %d: C2 [0x%07llx,0x%07llx] c2 [%.4f,%.4f]\n", n, (long long)C0, best, (long long)bm2, C2t, bm2 / C2t, bc2, nz, (long long)m2lo, (long long)m2hi, c2lo, c2hi);
    if (best) for (int j = 0; j < N; j++) { int a = as[j]; double e1 = (double)m1 * a / 2048, e2 = (double)bm2 * a * a / pow(2, 29), e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 E = 2 * C0 + tz(s1 * e1 + c1) + tz(-s1 * e1 + c1) + 2 * tz(-s2 * e2 + bc2) + tz(-s1 * e3 + c3) + tz(s1 * e3 + c3); if (E != Ev[j]) printf("  miss a=%d: E %lld hw %lld  e2 %.4f\n", a, (long long)E, (long long)Ev[j], e2); }
    return 0; }
