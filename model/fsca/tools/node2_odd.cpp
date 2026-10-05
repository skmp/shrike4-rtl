#include "fsca_common.h"
#include <cmath>
#include <vector>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main(int argc, char** argv) { int n = atoi(argv[1]); Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536, th = 2 * M_PI * n / 64;
    std::vector<int> as; std::vector<i64> Od; for (int a = 1; a <= 511; a++) { int ip = (n * 1024 + a) % 65536, im = (n * 1024 - a + 65536) % 65536; Win wp = decode(F.srz[ip], F.srn[ip]), wm = decode(F.srz[im], F.srn[im]); if (!wp.exact || !wm.exact) continue; as.push_back(a); Od.push_back(wp.sign * wp.lo - wm.sign * wm.lo); } int N = as.size();
    double C1t = cos(th) * k * pow(2, 37), C3t = fabs(cos(th)) * k * k * k / 6 * pow(2, 66); int s1 = cos(th) >= 0 ? 1 : -1; i64 m1t = (i64)floor(fabs(C1t));
    int best = 1 << 30; i64 bm1 = 0, bm3 = 0; double b1 = 0, b3 = 0; int nz = 0; double c1lo = 9, c1hi = -9, c3lo = 9, c3hi = -9; i64 m1lo = 1ll << 60, m1hi = -1, m3lo = 1ll << 60, m3hi = -1;
    for (i64 dm1 = -128; dm1 <= 128; dm1++) for (int i1 = 60; i1 <= 100; i1 += 2) for (int i3 = 4; i3 <= 16; i3 += 2) for (i64 m3 = (i64)(C3t * 0.998); m3 <= (i64)(C3t * 1.002); m3 += 16) { double c1 = i1 / 2048.0, c3 = i3 / 2048.0; int mism = 0;
        for (int j = 0; j < N && mism <= best; j++) { int a = as[j]; double e1 = (double)(m1t + dm1) * a / 2048, e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 O = tz(s1 * e1 + c1) - tz(-s1 * e1 + c1) + tz(-s1 * e3 + c3) - tz(s1 * e3 + c3); if (O != Od[j]) mism++; }
        if (mism < best) { best = mism; bm1 = m1t + dm1; bm3 = m3; b1 = c1; b3 = c3; } if (mism == 0) { nz++; if (c1 < c1lo) c1lo = c1; if (c1 > c1hi) c1hi = c1; if (c3 < c3lo) c3lo = c3; if (c3 > c3hi) c3hi = c3; if (m1t + dm1 < m1lo) m1lo = m1t + dm1; if (m1t + dm1 > m1hi) m1hi = m1t + dm1; if (m3 < m3lo) m3lo = m3; if (m3 > m3hi) m3hi = m3; } }
    printf("node %d ODD free constants: best %d C1 0x%06llx (%+lld) C3 0x%06llx c1 %.4f c3 %.4f | exact %d: C1 [0x%06llx,0x%06llx] C3 [0x%06llx,0x%06llx] c1 [%.4f,%.4f] c3 [%.4f,%.4f]\n", n, best, (long long)bm1, (long long)(bm1 - m1t), (long long)bm3, b1, b3, nz, (long long)m1lo, (long long)m1hi, (long long)m3lo, (long long)m3hi, c1lo, c1hi, c3lo, c3hi);
    if (best) { for (int j = 0; j < N; j++) { int a = as[j]; double e1 = (double)bm1 * a / 2048, e3 = (double)bm3 * a * (double)a * a / pow(2, 40); i64 O = tz(s1 * e1 + b1) - tz(-s1 * e1 + b1) + tz(-s1 * e3 + b3) - tz(s1 * e3 + b3); if (O != Od[j]) printf("  miss a=%d: O %lld hw %lld  e1 frac %.4f e3 %.3f\n", a, (long long)O, (long long)Od[j], e1 - floor(e1), e3); } }
    return 0; }
