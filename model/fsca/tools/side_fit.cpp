// per-side exact fit: V(s) = C0 + tz(s*s1*e1 + c1) + tz(-s2*e2 + c2) + tz(-s*s1*e3 + c3), C0 pinned; scan C1 +-6, C2 fine, C3 fine, c1,c2,c3 grids
#include "fsca_common.h"
#include <cmath>
#include <vector>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main(int argc, char** argv) { int n = atoi(argv[1]); i64 C0 = atoll(argv[2]); i64 m1c = strtoll(argv[3], 0, 0), m2c = strtoll(argv[4], 0, 0), m3c = strtoll(argv[5], 0, 0); Fsca F; if (!load_fsca(F)) return 1; double th = 2 * M_PI * n / 64; int s1 = cos(th) >= 0 ? 1 : -1, s2 = sin(th) >= 0 ? 1 : -1;
    for (int s : {1, -1}) { std::vector<int> as; std::vector<i64> Vs; for (int a = 1; a <= 511; a++) { int i = (n * 1024 + s * a + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; as.push_back(a); Vs.push_back(w.sign * w.lo); } int N = as.size();
        int best = 1 << 30; i64 b1 = 0, b2 = 0, b3 = 0; double bc1 = 0, bc2 = 0, bc3 = 0; int nz = 0; double c1lo = 9, c1hi = -9, c2lo = 9, c2hi = -9, c3lo = 9, c3hi = -9; i64 m2lo = 1ll << 60, m2hi = -1;
        for (i64 dm1 = -6; dm1 <= 6; dm1++) for (i64 m2 = m2c - 96; m2 <= m2c + 96; m2 += 4) for (i64 m3 = m3c - 64; m3 <= m3c + 64; m3 += 16) for (int i1 = 78; i1 <= 84; i1 += 2) for (int i2 = -24; i2 <= 24; i2 += 2) for (int i3 = -14; i3 <= 2; i3 += 2) { double c1 = i1 / 2048.0, c2 = i2 / 2048.0, c3 = i3 / 2048.0; int mism = 0;
            for (int j = 0; j < N && mism <= best; j++) { int a = as[j]; double e1 = (double)(m1c + dm1) * a / 2048, e2 = (double)m2 * a * a / pow(2, 29), e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 W = C0 + tz(s * s1 * e1 + c1) + tz(-s2 * e2 + c2) + tz(-s * s1 * e3 + c3); if (W != Vs[j]) mism++; }
            if (mism < best) { best = mism; b1 = m1c + dm1; b2 = m2; b3 = m3; bc1 = c1; bc2 = c2; bc3 = c3; } if (mism == 0) { nz++; if (c1 < c1lo) c1lo = c1; if (c1 > c1hi) c1hi = c1; if (c2 < c2lo) c2lo = c2; if (c2 > c2hi) c2hi = c2; if (c3 < c3lo) c3lo = c3; if (c3 > c3hi) c3hi = c3; if (m2 < m2lo) m2lo = m2; if (m2 > m2hi) m2hi = m2; } }
        printf("node %d side %+d (%d): best %d  C1 0x%06llx C2 0x%07llx C3 0x%06llx c1 %.5f c2 %.5f c3 %.5f | exact %d: c1 [%.5f,%.5f] c2 [%.5f,%.5f] c3 [%.5f,%.5f] C2 [0x%07llx,0x%07llx]\n", n, s, N, best, (long long)b1, (long long)b2, (long long)b3, bc1, bc2, bc3, nz, c1lo, c1hi, c2lo, c2hi, c3lo, c3hi, (long long)m2lo, (long long)m2hi); }
    return 0; }
