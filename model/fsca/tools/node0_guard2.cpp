#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1 = (i64)floor(k * pow(2, 37)); i64 m3 = 10838016; i64 V[2][512];
    for (int a = 1; a <= 511; a++) { Win wp = decode(F.srz[a], F.srn[a]), wm = decode(F.srz[65536 - a], F.srn[65536 - a]); V[0][a] = wp.sign * wp.lo; V[1][a] = wm.sign * wm.lo; }
    double ks[5] = { 0, 0.25, 0.5, 1, 2 }; int printed = 0;
    for (int g = 0; g <= 8; g++) for (double k1 : ks) for (double k3 : ks) { int best = 1 << 30; double bc = 0;
        for (int ic = -64; ic <= 256; ic++) { double c = ic / 256.0; int mism = 0; for (int a = 1; a <= 511 && mism <= best; a++) for (int si = 0; si < 2; si++) { int s = si ? -1 : 1; double e1 = (double)m1 * a / 2048, e3 = (double)m3 * a * (double)a * a / pow(2, 40);
            double t1 = (double)tz(s * e1 * (1 << g) + k1) / (1 << g), t3 = (double)tz(-s * e3 * (1 << g) + k3) / (1 << g); i64 W = tz(t1 + t3 + (s > 0 ? c : -c)); if (W != V[si][a]) mism++; } if (mism < best) { best = mism; bc = c; } }
        if (best <= 3) { printed++; printf("g=%d k1=%.2f k3=%.2f: mism %d at c=%.4f\n", g, k1, k3, best, bc); } }
    printf("%d configs with <=3 mismatches\n", printed); return 0; }
