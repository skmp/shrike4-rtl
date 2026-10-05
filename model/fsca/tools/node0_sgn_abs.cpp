// signed abstract model: T_i = trunc_toward_zero(P_i + c_i), P1 = s*C1*a, P3 = -s*C3*a^3 ; V = T1 + T3 on both sides
#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1 = (i64)floor(k * pow(2, 37)); i64 V[2][512];
    for (int a = 1; a <= 511; a++) { Win wp = decode(F.srz[a], F.srn[a]), wm = decode(F.srz[65536 - a], F.srn[65536 - a]); V[0][a] = wp.sign * wp.lo; V[1][a] = wm.sign * wm.lo; }
    int best = 1 << 30; double bc1 = 0, bc3 = 0; i64 bm3 = 0; int nz = 0; double c1lo = 9, c1hi = -9, c3lo = 9, c3hi = -9; i64 m3lo = 1ll << 60, m3hi = -1;
    for (i64 m3 = 10837900; m3 <= 10838100; m3 += 4) for (int i1 = 70; i1 <= 90; i1++) for (int i3 = -16; i3 <= 32; i3++) { double c1 = i1 / 2048.0, c3 = i3 / 2048.0; int mism = 0;
        for (int a = 1; a <= 511 && mism <= best; a++) for (int si = 0; si < 2; si++) { int s = si ? -1 : 1; double e1 = (double)m1 * a / 2048, e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 W = tz(s * e1 + c1) + tz(-s * e3 + c3); if (W != V[si][a]) mism++; }
        if (mism < best) { best = mism; bc1 = c1; bc3 = c3; bm3 = m3; } if (mism == 0) { nz++; if (c1 < c1lo) c1lo = c1; if (c1 > c1hi) c1hi = c1; if (c3 < c3lo) c3lo = c3; if (c3 > c3hi) c3hi = c3; if (m3 < m3lo) m3lo = m3; if (m3 > m3hi) m3hi = m3; } }
    printf("signed abstract (same constants both sides): best mism %d of 1022 at c1=%.5f c3=%.5f C3=%lld; zero-fits %d: c1 [%.5f,%.5f] c3 [%.5f,%.5f] C3 [%lld,%lld]\n", best, bc1, bc3, (long long)bm3, nz, c1lo, c1hi, c3lo, c3hi, (long long)m3lo, (long long)m3hi);
    // variant: constants flip sign with the product sign (i.e. magnitude + c): T = s*floor(|P| + c)
    best = 1 << 30; for (i64 m3 = 10837900; m3 <= 10838100; m3 += 4) for (int i1 = 70; i1 <= 90; i1++) for (int i3 = -16; i3 <= 32; i3++) { double c1 = i1 / 2048.0, c3 = i3 / 2048.0; int mism = 0;
        for (int a = 1; a <= 511 && mism <= best; a++) for (int si = 0; si < 2; si++) { int s = si ? -1 : 1; double e1 = (double)m1 * a / 2048, e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 W = s * (i64)floor(e1 + c1) - s * (i64)floor(e3 + c3); if (W != V[si][a]) mism++; }
        if (mism < best) { best = mism; bc1 = c1; bc3 = c3; bm3 = m3; } }
    printf("magnitude abstract (constants on magnitudes): best mism %d at c1=%.5f c3=%.5f C3=%lld\n", best, bc1, bc3, (long long)bm3);
    return 0; }
