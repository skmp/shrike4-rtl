#include "fsca_common.h"
#include <cmath>
#include <vector>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 C0 = 13092290, C1 = 0xc532d6; i64 C2c = 0x1ecd42a, C3c = 0xa22fed;
    for (int s : {1, -1}) { std::vector<int> as; std::vector<i64> Vs; for (int a = 1; a <= 511; a++) { int i = (2048 + s * a + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); as.push_back(a); Vs.push_back(w.sign * w.lo); }
        int best = 1 << 30; double b1 = 0, b2 = 0, b3 = 0; i64 bC2 = 0, bC3 = 0; int nz = 0; double lo[3] = {9,9,9}, hi[3] = {-9,-9,-9};
        for (i64 C2 = C2c - 32; C2 <= C2c + 32; C2 += 4) for (i64 C3 = C3c - 48; C3 <= C3c + 48; C3 += 16) for (int i1 = 56; i1 <= 90; i1 += 1) for (int i2 = 0; i2 <= 24; i2 += 1) for (int i3 = 6; i3 <= 24; i3 += 2) { double c1 = i1 / 2048.0, c2 = i2 / 2048.0, c3 = i3 / 2048.0; int mism = 0;
            for (size_t j = 0; j < as.size() && mism <= best; j++) { int a = as[j]; double e1 = (double)C1 * a / 2048, e2 = (double)C2 * a * a / pow(2, 29), e3 = (double)C3 * a * (double)a * a / pow(2, 40); i64 W = C0 + tz(s * e1 + c1) + tz(-e2 - c2) + tz(-s * e3 - c3); if (W != Vs[j]) mism++; }
            if (mism < best) { best = mism; b1 = c1; b2 = c2; b3 = c3; bC2 = C2; bC3 = C3; } if (mism == 0) { nz++; double cs[3] = { c1, c2, c3 }; for (int q = 0; q < 3; q++) { if (cs[q] < lo[q]) lo[q] = cs[q]; if (cs[q] > hi[q]) hi[q] = cs[q]; } } }
        printf("node 2 side %+d: best %d (c1 %.4f c2 %.4f c3 %.4f C2 %llx C3 %llx) | exact %d: c1 [%.4f,%.4f] c2 [%.4f,%.4f] c3 [%.4f,%.4f]\n", s, best, b1, b2, b3, (long long)bC2, (long long)bC3, nz, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]); }
    return 0; }
