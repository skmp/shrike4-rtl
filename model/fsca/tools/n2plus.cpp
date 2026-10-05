#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 C0 = 13092290, C1 = 0xc532d6, C3 = 0xa22fed;
    for (int s : {1, -1}) { int best = 1 << 30; double b1 = 0, b2 = 0, b3 = 0; i64 bC2 = 0; int nz = 0; double lo[3] = {9,9,9}, hi[3] = {-9,-9,-9};
        for (i64 C2 = 0x1ecd42a - 24; C2 <= 0x1ecd42a + 24; C2 += 2) for (int i1 = 112; i1 <= 132; i1++) for (int i2 = -20; i2 <= 4; i2++) for (int i3 = 22; i3 <= 32; i3++) { double c1 = i1 / 4096.0, c2 = i2 / 4096.0, c3 = i3 / 4096.0; int mism = 0;
            for (int a = 1; a <= 511 && mism <= best; a++) { int i = (2048 + s * a + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); i64 V = w.sign * w.lo; double e1 = (double)C1 * a / 2048, e2 = (double)C2 * a * a / pow(2, 29), e3 = (double)C3 * (double)a * a * a / pow(2, 40);
                i64 W = C0 + tz(s * e1 + c1) - tz(e2 + c2) - tz(s * e3 + c3); if (W != V) mism++; }   // sign-magnitude: sign(C)*tz(|C| t^k + c), C1>0, C2<0, C3<0
            if (mism < best) { best = mism; b1 = c1; b2 = c2; b3 = c3; bC2 = C2; } if (mism == 0) { nz++; double cs[3] = { c1, c2, c3 }; for (int q = 0; q < 3; q++) { if (cs[q] < lo[q]) lo[q] = cs[q]; if (cs[q] > hi[q]) hi[q] = cs[q]; } } }
        printf("node 2 side %+d: best %d (c1 %.4f c2 %.4f c3 %.4f C2 %llx) | exact %d: c1 [%.4f,%.4f] c2 [%.4f,%.4f] c3 [%.4f,%.4f]\n", s, best, b1, b2, b3, (long long)bC2, nz, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]); }
    return 0; }
