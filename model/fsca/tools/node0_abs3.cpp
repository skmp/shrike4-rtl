#include "fsca_common.h"
#include <cmath>
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 V[512]; for (int a = 1; a <= 511; a++) V[a] = decode(F.srz[a], F.srn[a]).lo;
    double k = 2 * M_PI / 65536; i64 m1 = (i64)floor(k * pow(2, 37)); double c3t = (k * k * k / 6) * pow(2, 66);
    int best = 1 << 30; double bs = 0, bc = 0; int nz = 0; double slo = 9, shi = -9, clo = 9, chi = -9;
    for (int is = 0; is <= 400; is++) { double s = 1.0 + is * 0.000001; for (int ic = -128; ic <= 256; ic++) { double c3 = ic / 2048.0; int m = 0;
        for (int a = 1; a <= 511 && m <= best; a++) { double e1 = (double)m1 * a / pow(2, 11); double e3 = c3t * s * a * (double)a * a / pow(2, 40); i64 W = (i64)floor(e1 + 5 / 128.0) - (i64)floor(e3 + c3); if (W != V[a]) m++; }
        if (m < best) { best = m; bs = s; bc = c3; } if (m == 0) { nz++; if (s < slo) slo = s; if (s > shi) shi = s; if (c3 < clo) clo = c3; if (c3 > chi) chi = c3; } } }
    printf("best mism %d at C3 scale %.6f c3=%.5f; zero-mismatch points %d: scale [%.6f,%.6f] c3 [%.5f,%.5f]\n", best, bs, bc, nz, slo, shi, clo, chi);
    printf("C3 true mantissa (2^-66) %.2f; scaled range %.1f .. %.1f\n", c3t, c3t * slo, c3t * shi); return 0; }
