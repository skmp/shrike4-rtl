#include "fsca_common.h"
#include <cmath>
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 V[512]; for (int a = 1; a <= 511; a++) V[a] = decode(F.srz[a], F.srn[a]).lo;
    double k = 2 * M_PI / 65536; i64 m1 = (i64)floor(k * pow(2, 37)); i64 m3 = (i64)llround((k * k * k / 6) * pow(2, 66));
    auto e1 = [&](int a) { return (double)m1 * a / pow(2, 11); }; auto e3 = [&](int a) { return (double)m3 * a * (double)a * a / pow(2, 40); };
    printf("A: residuals at c1=5/128, c3=0.0273:\n"); for (int a = 1; a <= 511; a++) { i64 W = (i64)floor(e1(a) + 5 / 128.0) - (i64)floor(e3(a) + 0.0273); if (W != V[a]) printf("  a=%d V=%lld W=%lld  e1 frac %.4f  e3=%.4f\n", a, (long long)V[a], (long long)W, e1(a) - floor(e1(a)), e3(a)); }
    int best = 1 << 30; double bc = 0; for (int i = 0; i <= 4096; i++) { double c = i / 4096.0 - 0.5; int m = 0; for (int a = 1; a <= 511; a++) { i64 W = (i64)floor(e1(a) - e3(a) + c); if (W != V[a]) m++; } if (m < best) { best = m; bc = c; } }
    printf("B single floor: best mism %d at c=%.5f\n", best, bc);
    best = 1 << 30; double b1 = 0, b3 = 0; for (int i1 = 0; i1 <= 64; i1++) { double c1 = 0.03 + i1 / 4096.0; for (int i3 = 0; i3 <= 256; i3++) { double c3 = i3 / 4096.0; int m = 0; for (int a = 1; a <= 511 && m <= best; a++) { i64 W = (i64)floor(e1(a) + c1) - (i64)floor(e3(a) + c3); if (W != V[a]) m++; } if (m < best) { best = m; b1 = c1; b3 = c3; } } }
    printf("A fine: best mism %d at c1=%.5f c3=%.5f\n", best, b1, b3);
    // implied cubic constant interval per a for c1 = 5/128
    double lo = -9, hi = 9; int alo = 0, ahi = 0; for (int a = 1; a <= 511; a++) { i64 T3 = (i64)floor(e1(a) + 5 / 128.0) - V[a]; double l = T3 - e3(a), h = T3 - e3(a) + 1; if (l > lo) { lo = l; alo = a; } if (h < hi) { hi = h; ahi = a; } }
    printf("cubic constant interval given c1=5/128: (%.4f, %.4f] binding a=%d,%d\n", lo, hi, alo, ahi);
    for (int a = 1; a <= 511; a++) { i64 T3 = (i64)floor(e1(a) + 5 / 128.0) - V[a]; double l = T3 - e3(a); if (l > 0.02 || l < -0.98) printf("  a=%d T3req=%lld e3=%.4f (needs c > %.4f)\n", a, (long long)T3, e3(a), l); }
    return 0; }
