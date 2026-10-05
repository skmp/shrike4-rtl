// node n signed abstract: V = C0 + tz(s*C1 a + c1) + tz(-C2 a^2 + c2) + tz(-s*C3 a^3 + c3)   (C2, C3 magnitudes; signs of the true terms)
#include "fsca_common.h"
#include <cmath>
#include <vector>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main(int argc, char** argv) { int n = atoi(argv[1]); Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536, th = 2 * M_PI * n / 64;
    std::vector<int> as, ss; std::vector<i64> Vs; for (int a = 1; a <= 511; a++) for (int s : {1, -1}) { int i = (n * 1024 + s * a + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; as.push_back(a); ss.push_back(s); Vs.push_back(w.sign * w.lo); } int N = as.size();
    i64 m1t = (i64)floor(cos(th) * k * pow(2, 37)); double c2t = sin(th) * k * k / 2 * pow(2, 55), m3t = cos(th) * k * k * k / 6 * pow(2, 66); i64 C0t = (i64)llround(sin(th) * pow(2, 26));
    printf("node %d: %d samples; true C0 %.3f C1 %lld C2 %.1f C3 %.1f\n", n, N, sin(th) * pow(2, 26), (long long)m1t, c2t, m3t);
    // stage A: coarse, c1=0.0395 c3=0.0045 fixed; scan C0 +-3, C1 +-16, C2 (2^-55) coarse +-0.05% step 64, c2 in 0..1 step 1/16, C3 +-0.05% step 512
    int best = 1 << 30; i64 bC0 = 0, bm1 = 0, bm2 = 0, bm3 = 0; double bc2 = 0;
    for (i64 dC0 = -3; dC0 <= 3; dC0++) for (i64 dm1 = -16; dm1 <= 16; dm1++) for (i64 m2 = (i64)(c2t * 0.9995); m2 <= (i64)(c2t * 1.0005); m2 += 64) for (int ic2 = 0; ic2 <= 16; ic2++) for (i64 m3 = (i64)(m3t * 0.9995); m3 <= (i64)(m3t * 1.0005); m3 += 512) { double c2 = ic2 / 16.0; int mism = 0;
        for (int j = 0; j < N && mism <= best; j++) { int a = as[j], s = ss[j]; double e1 = (double)(m1t + dm1) * a / 2048, e2 = (double)m2 * a * a / pow(2, 29), e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 W = C0t + dC0 + tz(s * e1 + 0.0395) + tz(-e2 + c2) + tz(-s * e3 + 0.0045); if (W != Vs[j]) mism++; }
        if (mism < best) { best = mism; bC0 = C0t + dC0; bm1 = m1t + dm1; bm2 = m2; bm3 = m3; bc2 = c2; } }
    printf("stage A best mism %d: C0 %lld C1 %lld (%+lld) C2 %lld (%.6f x true) c2 %.4f C3 %lld (%.6f x true)\n", best, (long long)bC0, (long long)bm1, (long long)(bm1 - m1t), (long long)bm2, bm2 / c2t, bc2, (long long)bm3, bm3 / m3t);
    // stage B: refine around best: C2 +-128 step 4, c2 fine, c1/c3 scans
    int best2 = 1 << 30; double b1 = 0, b2 = 0, b3 = 0; i64 bb2 = 0; int nz = 0; double c1lo = 9, c1hi = -9, c2lo = 9, c2hi = -9, c3lo = 9, c3hi = -9; i64 m2lo = 1ll << 60, m2hi = -1;
    for (i64 m2 = bm2 - 128; m2 <= bm2 + 128; m2 += 4) for (int i1 = 76; i1 <= 86; i1 += 2) for (int i2 = -64; i2 <= 320; i2 += 4) for (int i3 = 6; i3 <= 12; i3 += 2) { double c1 = i1 / 2048.0, c2 = i2 / 2048.0, c3 = i3 / 2048.0; int mism = 0;
        for (int j = 0; j < N && mism <= best2; j++) { int a = as[j], s = ss[j]; double e1 = (double)bm1 * a / 2048, e2 = (double)m2 * a * a / pow(2, 29), e3 = (double)bm3 * a * (double)a * a / pow(2, 40); i64 W = bC0 + tz(s * e1 + c1) + tz(-e2 + c2) + tz(-s * e3 + c3); if (W != Vs[j]) mism++; }
        if (mism < best2) { best2 = mism; b1 = c1; b2 = c2; b3 = c3; bb2 = m2; } if (mism == 0) { nz++; if (c1 < c1lo) c1lo = c1; if (c1 > c1hi) c1hi = c1; if (c2 < c2lo) c2lo = c2; if (c2 > c2hi) c2hi = c2; if (c3 < c3lo) c3lo = c3; if (c3 > c3hi) c3hi = c3; if (m2 < m2lo) m2lo = m2; if (m2 > m2hi) m2hi = m2; } }
    printf("stage B best mism %d: c1 %.5f c2 %.5f c3 %.5f C2 %lld | zero-fits %d: c1 [%.5f,%.5f] c2 [%.5f,%.5f] c3 [%.5f,%.5f] C2 [%lld,%lld]\n", best2, b1, b2, b3, (long long)bb2, nz, c1lo, c1hi, c2lo, c2hi, c3lo, c3hi, (long long)m2lo, (long long)m2hi);
    return 0; }
