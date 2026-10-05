#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 C0 = 13092290; int s = 1; i64 V[512]; for (int a = 1; a <= 511; a++) { Win w = decode(F.srz[2048 + a], F.srn[2048 + a]); V[a] = w.sign * w.lo; }
    int best = 1 << 30; i64 b1 = 0, b2 = 0, b3 = 0; double c1b = 0, c2b = 0, c3b = 0;
    for (i64 C1 = 0xc532d6 - 8; C1 <= 0xc532d6 + 8; C1++) for (i64 A2 = 0x1ecd432 - 64; A2 <= 0x1ecd432 + 64; A2 += 4) for (i64 A3 = 0xa22fed - 1024; A3 <= 0xa22fed + 1024; A3 += 32) for (int i1 = 56; i1 <= 70; i1 += 2) for (int i2 = -4; i2 <= 12; i2 += 2) for (int i3 = 0; i3 <= 16; i3 += 2) { double c1 = i1 / 2048.0, c2 = i2 / 2048.0, c3 = i3 / 2048.0; int mism = 0;
        for (int a = 1; a <= 511 && mism <= best; a++) { double e1 = (double)C1 * a / 2048, e2 = (double)A2 * a * a / pow(2, 29), e3 = (double)A3 * (double)a * a * a / pow(2, 40); i64 W = C0 + tz(s * e1 + c1) - tz(e2 + c2) - tz(s * e3 + c3); if (W != V[a]) mism++; }
        if (mism < best) { best = mism; b1 = C1; b2 = A2; b3 = A3; c1b = c1; c2b = c2; c3b = c3; if (!best) break; } }
    printf("node 2 + side wide: best %d  C1 0x%llx A2 0x%llx A3 0x%llx c1 %.4f c2 %.4f c3 %.4f\n", best, (long long)b1, (long long)b2, (long long)b3, c1b, c2b, c3b); return 0; }
