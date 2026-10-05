#include "fsca_common.h"
#include <cmath>
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 V[512]; for (int a = 1; a <= 511; a++) V[a] = decode(F.srz[a], F.srn[a]).lo;
    double k = 2 * M_PI / 65536; double c1t = k * pow(2, 37), c3t = (k * k * k / 6) * pow(2, 66);
    for (int m1mode = 0; m1mode < 3; m1mode++) { i64 m1 = m1mode == 0 ? (i64)floor(c1t) : m1mode == 1 ? (i64)llround(c1t) : (i64)floor(c1t) - 1;
        for (double s3 : {1.0, 1.0005, 1.001, 1.002, 0.999}) { i64 m3 = (i64)llround(c3t * s3);
            int best = 1 << 30; double bc1 = 0, bc3 = 0; int nfeas = 0; double c1lo = 9, c1hi = -9, c3lo = 9, c3hi = -9;
            for (int i1 = 0; i1 <= 256; i1++) { double c1 = i1 / 2048.0; for (int i3 = 0; i3 <= 256; i3++) { double c3 = i3 / 256.0; int mism = 0;
                for (int a = 1; a <= 511 && mism <= best; a++) { double e1 = (double)m1 * a / pow(2, 11); double e3 = (double)m3 * a * (double)a * a / pow(2, 40); i64 W = (i64)floor(e1 + c1) - (i64)floor(e3 + c3); if (W != V[a]) mism++; }
                if (mism < best) { best = mism; bc1 = c1; bc3 = c3; } if (mism == 0) { nfeas++; if (c1 < c1lo) c1lo = c1; if (c1 > c1hi) c1hi = c1; if (c3 < c3lo) c3lo = c3; if (c3 > c3hi) c3hi = c3; } } }
            printf("m1 %s (%lld) C3 x%.4f: best mism %d at c1=%.5f c3=%.4f; exact-fit grid points %d, c1 in [%.5f,%.5f] c3 in [%.4f,%.4f]\n", m1mode == 0 ? "trunc" : m1mode == 1 ? "round" : "trunc-1", (long long)m1, s3, best, bc1, bc3, nfeas, c1lo, c1hi, c3lo, c3hi); } }
    return 0; }
