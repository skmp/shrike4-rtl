#include "fsca_common.h"
#include <cmath>
#include <vector>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main(int argc, char** argv) { int n = atoi(argv[1]); i64 C0 = atoll(argv[2]); i64 m1c = strtoll(argv[3], 0, 0), m2c = strtoll(argv[4], 0, 0), m3c = strtoll(argv[5], 0, 0); Fsca F; if (!load_fsca(F)) return 1; double th = 2 * M_PI * n / 64; int s1 = cos(th) >= 0 ? 1 : -1, s2 = sin(th) >= 0 ? 1 : -1;
    for (int s : {1, -1}) { std::vector<int> as; std::vector<i64> Vs; for (int a = 1; a <= 511; a++) { int i = (n * 1024 + s * a + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; as.push_back(a); Vs.push_back(w.sign * w.lo); } int N = as.size();
        int best = 1 << 30; i64 b1 = 0; double bc1 = 0; int nz = 0; double c1lo = 9, c1hi = -9; i64 m1lo = 1ll << 60, m1hi = -1; double c2b = 0, c3b = 0;
        for (i64 dm1 = -24; dm1 <= 24; dm1++) for (i64 m2 = m2c - 64; m2 <= m2c + 64; m2 += 8) for (i64 m3 = m3c - 48; m3 <= m3c + 48; m3 += 24) for (int i1 = 0; i1 <= 164; i1 += 1) for (int i2 = -12; i2 <= 12; i2 += 4) for (int i3 = -12; i3 <= 0; i3 += 4) { double c1 = i1 / 2048.0, c2 = i2 / 2048.0, c3 = i3 / 2048.0; int mism = 0;
            for (int j = 0; j < N && mism <= best; j++) { int a = as[j]; double e1 = (double)(m1c + dm1) * a / 2048, e2 = (double)m2 * a * a / pow(2, 29), e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 W = C0 + tz(s * s1 * e1 + c1) + tz(-s2 * e2 + c2) + tz(-s * s1 * e3 + c3); if (W != Vs[j]) mism++; }
            if (mism < best) { best = mism; b1 = m1c + dm1; bc1 = c1; c2b = c2; c3b = c3; } if (mism == 0) { nz++; if (c1 < c1lo) c1lo = c1; if (c1 > c1hi) c1hi = c1; if (m1c + dm1 < m1lo) m1lo = m1c + dm1; if (m1c + dm1 > m1hi) m1hi = m1c + dm1; } }
        printf("node %2d side %+d (%d): best %d  C1 0x%06llx c1 %.5f (c2 %.4f c3 %.4f) | exact %d: C1 [0x%06llx,0x%06llx] c1 [%.5f,%.5f] = [%.1f,%.1f]/2048\n", n, s, N, best, (long long)b1, bc1, c2b, c3b, nz, (long long)m1lo, (long long)m1hi, c1lo, c1hi, c1lo * 2048, c1hi * 2048); }
    return 0; }
