// abstract per-product fit for node n (exact region only): V = C0 + s*floor(C1 a + c1) - floor(C2 a^2 + c2) - s*floor(C3 a^3 + c3)
#include "fsca_common.h"
#include <cmath>
#include <vector>
int main(int argc, char** argv) { int n = atoi(argv[1]); Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536, th = 2 * M_PI * n / 64;
    std::vector<int> as; std::vector<int> ss; std::vector<i64> Vs; for (int a = 1; a <= 511; a++) for (int s : {1, -1}) { int i = n * 1024 + s * a; if (i < 0 || i > 65535) continue; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; as.push_back(a); ss.push_back(s); Vs.push_back(w.sign * w.lo); }
    int N = as.size(); printf("node %d: %d exact samples; true C0=%.3f u, C1 mant %.2f (2^-37), C2 = %.6e (mant %.1f at 2^-55), C3 mant %.1f (2^-66)\n", n, N, sin(th) * pow(2, 26), cos(th) * k * pow(2, 37), sin(th) * k * k / 2, sin(th) * k * k / 2 * pow(2, 55), cos(th) * k * k * k / 6 * pow(2, 66));
    i64 m1t = (i64)floor(cos(th) * k * pow(2, 37)); double c2t = sin(th) * k * k / 2 * pow(2, 55); i64 m3t = (i64)llround(cos(th) * k * k * k / 6 * pow(2, 66)); i64 C0t = (i64)llround(sin(th) * pow(2, 26));
    // stage 1: for each (m1 delta, C0 delta) with c1 = 0.04, c3 = 0.008, find C2 (mantissa at 2^-55, coarse step 2^11 = 12-bit) and c2 minimising mismatches
    int best = 1 << 30; struct P { i64 dm1, dC0, m2, dm3; double c2; } bp = {0,0,0,0,0};
    for (i64 dm1 = -4; dm1 <= 4; dm1++) for (i64 dC0 = -3; dC0 <= 3; dC0++) for (i64 dm3 = -4096; dm3 <= 4096; dm3 += 2048) for (i64 m2 = (i64)(c2t * 0.999) & ~2047; m2 <= (i64)(c2t * 1.001); m2 += 2048) for (int ic2 = 0; ic2 <= 32; ic2++) { double c2 = ic2 / 32.0; int mism = 0;
        for (int j = 0; j < N && mism <= best; j++) { int a = as[j], s = ss[j]; double e1 = (double)(m1t + dm1) * a / pow(2, 11), e2 = (double)m2 * a * a / pow(2, 29), e3 = (double)(m3t + dm3) * a * (double)a * a / pow(2, 40);
            i64 W = C0t + dC0 + s * (i64)floor(e1 + 0.04) - (i64)floor(e2 + c2) - s * (i64)floor(e3 + 0.008); if (W != Vs[j]) mism++; }
        if (mism < best) { best = mism; bp = { dm1, dC0, m2, dm3, c2 }; } }
    printf("stage1 best mism %d: dm1 %lld dC0 %lld C2 mant %lld (%.6f x true) dm3 %lld c2 %.3f\n", best, (long long)bp.dm1, (long long)bp.dC0, (long long)bp.m2, bp.m2 / c2t, (long long)bp.dm3, bp.c2);
    // stage 2: refine C2 finely and constants
    int best2 = 1 << 30; i64 bm2 = 0; double bc1 = 0, bc2 = 0, bc3 = 0; i64 m2lo = 0, m2hi = 0; double c1lo = 9, c1hi = -9, c2lo = 9, c2hi = -9; int nz = 0;
    for (i64 m2 = bp.m2 - 2048; m2 <= bp.m2 + 2048; m2 += 64) for (int i1 = 76; i1 <= 88; i1++) for (int i2 = 0; i2 <= 64; i2++) for (double c3 : {0.005, 0.008}) { double c1 = i1 / 2048.0, c2 = i2 / 64.0; int mism = 0;
        for (int j = 0; j < N && mism <= best2; j++) { int a = as[j], s = ss[j]; double e1 = (double)(m1t + bp.dm1) * a / pow(2, 11), e2 = (double)m2 * a * a / pow(2, 29), e3 = (double)(m3t + bp.dm3) * a * (double)a * a / pow(2, 40);
            i64 W = C0t + bp.dC0 + s * (i64)floor(e1 + c1) - (i64)floor(e2 + c2) - s * (i64)floor(e3 + c3); if (W != Vs[j]) mism++; }
        if (mism < best2) { best2 = mism; bm2 = m2; bc1 = c1; bc2 = c2; bc3 = c3; } if (mism == 0) { nz++; if (!nz || m2 < m2lo) m2lo = m2; if (m2 > m2hi) m2hi = m2; if (c1 < c1lo) c1lo = c1; if (c1 > c1hi) c1hi = c1; if (c2 < c2lo) c2lo = c2; if (c2 > c2hi) c2hi = c2; } }
    printf("stage2 best mism %d: C2 mant %lld c1 %.5f c2 %.4f c3 %.3f; zero-fit points %d: C2 [%lld,%lld] c1 [%.5f,%.5f] c2 [%.4f,%.4f]\n", best2, (long long)bm2, bc1, bc2, bc3, nz, (long long)m2lo, (long long)m2hi, c1lo, c1hi, c2lo, c2hi);
    printf("C0 = %lld (true %.3f), C1 mant = %lld (trunc true %+lld), C3 mant = %lld (%+lld)\n", (long long)(C0t + bp.dC0), sin(th) * pow(2, 26), (long long)(m1t + bp.dm1), (long long)bp.dm1, (long long)(m3t + bp.dm3), (long long)bp.dm3);
    return 0; }
