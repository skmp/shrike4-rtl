// decomposition fit with fixed c1, c3: odd part -> C1, C3 ; even part -> C0, C2, c2
#include "fsca_common.h"
#include <cmath>
#include <vector>
int main(int argc, char** argv) { int n = atoi(argv[1]); double c1 = argc > 2 ? atof(argv[2]) : 0.0395, c3 = argc > 3 ? atof(argv[3]) : 0.0045; Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536, th = 2 * M_PI * n / 64;
    std::vector<int> as; std::vector<i64> Vp, Vm; for (int a = 1; a <= 511; a++) { int ip = (n * 1024 + a) % 65536, im = (n * 1024 - a + 65536) % 65536; Win wp = decode(F.srz[ip], F.srn[ip]), wm = decode(F.srz[im], F.srn[im]); if (!wp.exact || !wm.exact) continue; as.push_back(a); Vp.push_back(wp.sign * wp.lo); Vm.push_back(wm.sign * wm.lo); } int N = as.size();
    double C1t = cos(th) * k * pow(2, 37), C3t = fabs(cos(th)) * k * k * k / 6 * pow(2, 66), C2t = fabs(sin(th)) * k * k / 2 * pow(2, 55), C0t = sin(th) * pow(2, 26);
    int s1 = cos(th) >= 0 ? 1 : -1, s2 = sin(th) >= 0 ? 1 : -1;   // sign of linear coefficient; sign of C0 (square term has sign -s2, cubic -s1)
    // ODD: Vp - Vm = [floor(e1+c1) + floor(e1-c1)]*s1 - s1*[floor(e3-c3) + floor(e3+c3)]  (with c's applied on the signed products)
    i64 m1t = (i64)floor(fabs(C1t)); int best = 1 << 30; i64 bm1 = 0, bm3 = 0; int nz = 0; i64 m1lo = 1ll << 60, m1hi = -1, m3lo = 1ll << 60, m3hi = -1;
    for (i64 dm1 = -48; dm1 <= 48; dm1++) for (i64 m3 = (i64)(C3t * 0.999); m3 <= (i64)(C3t * 1.001); m3 += 16) { int mism = 0;
        for (int j = 0; j < N && mism <= best; j++) { int a = as[j]; double e1 = (double)(m1t + dm1) * a / 2048, e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 O = s1 * ((i64)floor(e1 + c1) + (i64)floor(e1 - c1)) - s1 * ((i64)floor(e3 - c3) + (i64)floor(e3 + c3)); if (O != Vp[j] - Vm[j]) mism++; }
        if (mism < best) { best = mism; bm1 = m1t + dm1; bm3 = m3; } if (mism == 0) { nz++; if (m1t + dm1 < m1lo) m1lo = m1t + dm1; if (m1t + dm1 > m1hi) m1hi = m1t + dm1; if (m3 < m3lo) m3lo = m3; if (m3 > m3hi) m3hi = m3; } }
    printf("node %2d (%d pairs) ODD: best %d  C1 %lld=0x%06llx (true %.1f, %+lld) C3 %lld (true %.1f) | exact: C1 [%lld,%lld] C3 [%lld,%lld]\n", n, N, best, (long long)bm1, (long long)bm1, fabs(C1t), (long long)(bm1 - m1t), (long long)bm3, C3t, (long long)m1lo, (long long)m1hi, (long long)m3lo, (long long)m3hi);
    // EVEN: Vp + Vm = 2C0 + s1*[floor(e1+c1) - floor(e1-c1)] - 2*s2*floor(e2 - c2') ... with product -s2*C2 a^2 + c2 truncated toward zero: tz(-s2*e2 + c2) both sides same
    i64 C0r = (i64)llround(C0t); int best2 = 1 << 30; i64 bC0 = 0, bm2 = 0; double bc2 = 0; int nz2 = 0; i64 m2lo = 1ll << 60, m2hi = -1; double c2lo = 9, c2hi = -9;
    for (i64 dC0 = -3; dC0 <= 3; dC0++) for (i64 m2 = (i64)(C2t * 0.9995); m2 <= (i64)(C2t * 1.0005); m2 += 4) for (int ic = -32; ic <= 96; ic++) { double c2 = ic / 1024.0; int mism = 0;
        for (int j = 0; j < N && mism <= best2; j++) { int a = as[j]; double e1 = (double)bm1 * a / 2048, e2 = (double)m2 * a * a / pow(2, 29), e3 = (double)bm3 * a * (double)a * a / pow(2, 40); double x2 = -s2 * e2 + c2; i64 t2 = x2 >= 0 ? (i64)floor(x2) : -(i64)floor(-x2);
            i64 E = 2 * (C0r + dC0) + s1 * ((i64)floor(e1 + c1) - (i64)floor(e1 - c1)) + 2 * t2 - s1 * ((i64)floor(e3 - c3) - (i64)floor(e3 + c3)); if (E != Vp[j] + Vm[j]) mism++; }
        if (mism < best2) { best2 = mism; bC0 = C0r + dC0; bm2 = m2; bc2 = c2; } if (mism == 0) { nz2++; if (m2 < m2lo) m2lo = m2; if (m2 > m2hi) m2hi = m2; if (c2 < c2lo) c2lo = c2; if (c2 > c2hi) c2hi = c2; } }
    printf("         EVEN: best %d  C0 %lld (true %.3f) C2 %lld=0x%06llx (true %.1f) c2 %.4f | exact: C2 [%lld,%lld] c2 [%.4f,%.4f]\n", best2, (long long)bC0, C0t, (long long)bm2, (long long)bm2, C2t, bc2, (long long)m2lo, (long long)m2hi, c2lo, c2hi);
    return 0; }
