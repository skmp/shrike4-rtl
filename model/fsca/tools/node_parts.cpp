// even/odd decomposition fit for node n on the exact region
#include "fsca_common.h"
#include <cmath>
#include <vector>
int main(int argc, char** argv) { int n = atoi(argv[1]); Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536, th = 2 * M_PI * n / 64;
    std::vector<int> as; std::vector<i64> Ev, Od; int nonint = 0;
    for (int a = 1; a <= 511; a++) { int ip = n * 1024 + a, im = n * 1024 - a; if (im < 0) im += 65536; if (ip > 65535) ip -= 65536; Win wp = decode(F.srz[ip], F.srn[ip]), wm = decode(F.srz[im], F.srn[im]); if (!wp.exact || !wm.exact) continue;
        i64 Vp = wp.sign * wp.lo, Vm = wm.sign * wm.lo; if ((Vp + Vm) & 1) nonint++; as.push_back(a); Ev.push_back((Vp + Vm) / 2); Od.push_back((Vp - Vm) / 2); }
    int N = as.size(); printf("node %d: %d exact pairs, non-integer even parts: %d\n", n, N, nonint);
    // EVEN: Ev(a) = C0 - floor(C2 a^2 + c2), C2 = m2 * 2^-55 -> e2 = m2 a^2 / 2^29
    double c2t = sin(th) * k * k / 2 * pow(2, 55); i64 C0t = (i64)llround(sin(th) * pow(2, 26));
    { int best = 1 << 30; i64 bC0 = 0, bm2 = 0; double bc2 = 0; i64 m2lo = 1ll << 60, m2hi = -1; double c2lo = 9, c2hi = -9; i64 C0lo = 1ll << 60, C0hi = -1; int nz = 0;
      for (i64 dC0 = -4; dC0 <= 4; dC0++) for (i64 m2 = (i64)(c2t * 0.998); m2 <= (i64)(c2t * 1.002); m2 += 8) for (int ic = 0; ic <= 128; ic++) { double c2 = ic / 128.0; int mism = 0;
          for (int j = 0; j < N && mism <= best; j++) { double e2 = (double)m2 * as[j] * as[j] / pow(2, 29); i64 W = C0t + dC0 - (i64)floor(e2 + c2); if (W != Ev[j]) mism++; }
          if (mism < best) { best = mism; bC0 = C0t + dC0; bm2 = m2; bc2 = c2; } if (mism == 0) { nz++; if (m2 < m2lo) m2lo = m2; if (m2 > m2hi) m2hi = m2; if (c2 < c2lo) c2lo = c2; if (c2 > c2hi) c2hi = c2; if (C0t + dC0 < C0lo) C0lo = C0t + dC0; if (C0t + dC0 > C0hi) C0hi = C0t + dC0; } }
      printf("EVEN best mism %d: C0 %lld (true %.3f) C2 mant %lld (%.6f x true) c2 %.4f | zero-fit %d: C0 [%lld,%lld] C2 [%lld,%lld] c2 [%.4f,%.4f]\n", best, (long long)bC0, sin(th) * pow(2, 26), (long long)bm2, bm2 / c2t, bc2, nz, (long long)C0lo, (long long)C0hi, (long long)m2lo, (long long)m2hi, c2lo, c2hi); }
    // ODD: Od(a) = floor(C1 a + c1) - floor(C3 a^3 + c3)
    i64 m1t = (i64)floor(cos(th) * k * pow(2, 37)); double m3t = cos(th) * k * k * k / 6 * pow(2, 66);
    { int best = 1 << 30; i64 bm1 = 0, bm3 = 0; double bc1 = 0, bc3 = 0; int nz = 0; i64 m1lo = 1ll << 60, m1hi = -1; double c1lo = 9, c1hi = -9, c3lo = 9, c3hi = -9; i64 m3lo = 1ll << 60, m3hi = -1;
      for (i64 dm1 = -64; dm1 <= 64; dm1++) for (int i1 = 70; i1 <= 90; i1++) for (i64 m3 = (i64)(m3t * 0.98); m3 <= (i64)(m3t * 1.02); m3 += 1024) for (int i3 = 0; i3 <= 16; i3++) { double c1 = i1 / 2048.0, c3 = i3 / 256.0; int mism = 0;
          for (int j = 0; j < N && mism <= best; j++) { int a = as[j]; double e1 = (double)(m1t + dm1) * a / pow(2, 11), e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 W = (i64)floor(e1 + c1) - (i64)floor(e3 + c3); if (W != Od[j]) mism++; }
          if (mism < best) { best = mism; bm1 = m1t + dm1; bm3 = m3; bc1 = c1; bc3 = c3; } if (mism == 0) { nz++; i64 m1 = m1t + dm1; if (m1 < m1lo) m1lo = m1; if (m1 > m1hi) m1hi = m1; if (c1 < c1lo) c1lo = c1; if (c1 > c1hi) c1hi = c1; if (c3 < c3lo) c3lo = c3; if (c3 > c3hi) c3hi = c3; if (m3 < m3lo) m3lo = m3; if (m3 > m3hi) m3hi = m3; } }
      printf("ODD best mism %d: C1 mant %lld (trunc true %+lld) c1 %.5f C3 mant %lld (%.5f x true) c3 %.4f | zero-fit %d: C1 [%lld,%lld] c1 [%.5f,%.5f] C3 [%lld,%lld] c3 [%.4f,%.4f]\n", best, (long long)bm1, (long long)(bm1 - m1t), bc1, (long long)bm3, bm3 / m3t, bc3, nz, (long long)m1lo, (long long)m1hi, c1lo, c1hi, (long long)m3lo, (long long)m3hi, c3lo, c3hi); }
    return 0; }
