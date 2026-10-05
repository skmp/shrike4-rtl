#include "fsca_common.h"
#include <cmath>
#include <vector>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline i64 rndf(i64 x, int w) { if (!x) return 0; u64 m = x < 0 ? -x : x; int p = msb(m); int sh = p - (w - 1); if (sh <= 0) return x; u64 q = m >> sh, r = m & ((1ull << sh) - 1); return x >= 0 ? (i64)(q << sh) : -(i64)((q + (r ? 1 : 0)) << sh); }
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 C0 = 13092290, C1 = 0xc532d6, A3 = 0xa22fed; std::vector<int> ts; std::vector<i64> Vs; for (int t = -512; t <= 511; t++) { int i = 2048 + t; Win w = decode(F.srz[i], F.srn[i]); ts.push_back(t); Vs.push_back(w.sign * w.lo); }
    for (int w : {21, 23, 25, 27}) { int best = 1 << 30; double b1 = 0, b2 = 0, b3 = 0; i64 bA2 = 0; int nz = 0; double lo[3] = {9,9,9}, hi[3] = {-9,-9,-9};
        for (i64 A2 = 0x1ecd42a - 16; A2 <= 0x1ecd42a + 16; A2 += 2) for (int i1 = 114; i1 <= 136; i1++) for (int i2 = -8; i2 <= 16; i2++) for (int i3 = 12; i3 <= 33; i3++) { double c1 = i1 / 4096.0, c2 = i2 / 4096.0, c3 = i3 / 4096.0; int mism = 0;
            for (size_t j = 0; j < ts.size() && mism <= best; j++) { i64 t = ts[j]; double e1 = (double)C1 * t / 2048; i64 u2 = rndf(A2 * t, w); double X2 = (double)u2 * t / pow(2, 29); i64 u3 = rndf(A3 * t, w); i64 u3b = rndf(u3 * t, w); double X3 = (double)u3b * t / pow(2, 40);
                i64 W = C0 + tz(e1 + c1) - tz(X2 + c2) - tz(X3 + c3); if (W != Vs[j]) mism++; }
            if (mism < best) { best = mism; b1 = c1; b2 = c2; b3 = c3; bA2 = A2; } if (mism == 0) { nz++; double cs[3] = { c1, c2, c3 }; for (int q = 0; q < 3; q++) { if (cs[q] < lo[q]) lo[q] = cs[q]; if (cs[q] > hi[q]) hi[q] = cs[q]; } } }
        printf("node 2 chain floor w=%d: best %d (c1 %.4f c2 %.4f c3 %.4f |C2| %llx) | exact %d: c1 [%.4f,%.4f] c2 [%.4f,%.4f] c3 [%.4f,%.4f]\n", w, best, b1, b2, b3, (long long)bA2, nz, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]); }
    return 0; }
