// per-side abstract fit: side s of node n: |V| = |C0 + s*T1 - T2 - s*T3| with T_i = floor(C_i a^i + c_i); C0, C2 taken from even fit (node 0: 0)
#include "fsca_common.h"
#include <cmath>
#include <vector>
int main(int argc, char** argv) { int n = atoi(argv[1]); i64 C0 = atoll(argv[2]); i64 m2 = atoll(argv[3]); double c2 = atof(argv[4]); Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536, th = 2 * M_PI * n / 64;
    i64 m1t = (i64)floor(cos(th) * k * pow(2, 37)); double m3t = cos(th) * k * k * k / 6 * pow(2, 66);
    for (int s : {1, -1}) { std::vector<int> as; std::vector<i64> Vs; for (int a = 1; a <= 511; a++) { int i = (n * 1024 + s * a + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; as.push_back(a); Vs.push_back(w.sign * w.lo); } int N = as.size();
        int best = 1 << 30; i64 bm1 = 0, bm3 = 0; double bc1 = 0, bc3 = 0; int nz = 0; double c1lo = 9, c1hi = -9, c3lo = 9, c3hi = -9; i64 m1lo = 1ll << 60, m1hi = -1;
        for (i64 dm1 = -12; dm1 <= 12; dm1++) for (int i1 = -8; i1 <= 100; i1++) for (i64 m3 = (i64)(m3t * 0.9995); m3 <= (i64)(m3t * 1.0005); m3 += 256) for (int i3 = -2; i3 <= 8; i3++) { double c1 = i1 / 2048.0, c3 = i3 / 256.0; int mism = 0;
            for (int j = 0; j < N && mism <= best; j++) { int a = as[j]; double e1 = (double)(m1t + dm1) * a / pow(2, 11), e2 = (double)m2 * a * a / pow(2, 29), e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 W = C0 + s * (i64)floor(e1 + c1) - (i64)floor(e2 + c2) - s * (i64)floor(e3 + c3); if (W != Vs[j]) mism++; }
            if (mism < best) { best = mism; bm1 = m1t + dm1; bm3 = m3; bc1 = c1; bc3 = c3; } if (mism == 0) { nz++; if (c1 < c1lo) c1lo = c1; if (c1 > c1hi) c1hi = c1; if (c3 < c3lo) c3lo = c3; if (c3 > c3hi) c3hi = c3; if (m1t + dm1 < m1lo) m1lo = m1t + dm1; if (m1t + dm1 > m1hi) m1hi = m1t + dm1; } }
        printf("node %d side %+d (%d samples): best mism %d: C1 %lld (%+lld) c1 %.5f C3 %lld c3 %.4f | zero-fits %d: C1 [%lld,%lld] c1 [%.5f,%.5f] c3 [%.4f,%.4f]\n", n, s, N, best, (long long)bm1, (long long)(bm1 - m1t), bc1, (long long)bm3, bc3, nz, (long long)m1lo, (long long)m1hi, c1lo, c1hi, c3lo, c3hi); }
    return 0; }
