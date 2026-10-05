#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1 = (i64)floor(k * pow(2, 37)); i64 V[2][512]; for (int a = 1; a <= 511; a++) { Win wp = decode(F.srz[a], F.srn[a]), wm = decode(F.srz[65536 - a], F.srn[65536 - a]); V[0][a] = wp.sign * wp.lo; V[1][a] = wm.sign * wm.lo; }
    for (double ratio : {4.0, 8.0, 10.0, 16.0}) { int best = 1 << 30; double bc = 0; i64 bm3 = 0; for (i64 m3 = 10837960; m3 <= 10838080; m3 += 4) for (int i1 = 78; i1 <= 84; i1++) { double c1 = i1 / 2048.0, c3 = c1 / ratio; int mism = 0; for (int a = 1; a <= 511; a++) for (int si = 0; si < 2; si++) { int s = si ? -1 : 1; double e1 = (double)m1 * a / 2048, e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 W = tz(s * e1 + c1) + tz(-s * e3 - c3); if (W != V[si][a]) mism++; } if (mism < best) { best = mism; bc = c1; bm3 = m3; } } printf("c3 = c1/%.0f: best mism %d (c1 %.5f, C3 %lld)\n", ratio, best, bc, (long long)bm3); }
    return 0; }
