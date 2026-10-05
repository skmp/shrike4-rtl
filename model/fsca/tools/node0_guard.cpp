#include "fsca_common.h"
#include <cmath>
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 V[512]; for (int a = 1; a <= 511; a++) V[a] = decode(F.srz[a], F.srn[a]).lo;
    double k = 2 * M_PI / 65536; i64 m1 = (i64)floor(k * pow(2, 37)); i64 m3 = 0xA56;
    for (int g1 = 0; g1 <= 8; g1++) for (int g3 = 0; g3 <= 8; g3++) for (double k1 : {0.0, 0.5}) for (double k3 : {0.0, 0.5}) { int best = 1 << 30; double bc = 0;
        for (int ic = 0; ic <= 512; ic++) { double c = ic / 512.0; int m = 0; for (int a = 1; a <= 511 && m <= best; a++) { double e1 = (double)m1 * a / pow(2, 11), e3 = (double)m3 * a * (double)a * a / pow(2, 28);
            double t1 = floor(e1 * (1 << g1) + k1) / (1 << g1), t3 = floor(e3 * (1 << g3) + k3) / (1 << g3); i64 W = (i64)floor(t1 - t3 + c); if (W != V[a]) m++; } if (m < best) { best = m; bc = c; } }
        if (best <= 2) printf("g1 %d g3 %d k1 %.1f k3 %.1f: mism %d at c=%.4f\n", g1, g3, k1, k3, best, bc); }
    return 0; }
