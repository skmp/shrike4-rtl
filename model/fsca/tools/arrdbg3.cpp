#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; load_fsca(F); g_K = 0; i64 A1 = 0xc81800, A2 = 0xf79b60, A3 = 0xa49008, C0 = 6577819; int shown = 0, miss = 0, tot = 0;
    for (int t = -512; t <= 511; t++) { int i = (1024 + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; tot++; i64 V = w.sign * w.lo; int a = t < 0 ? -t : t, s = t < 0 ? -1 : 1;
        uint32_t sq = (uint32_t)(a * a) << 5; i64 p = mult28((uint32_t)A2, sq); i64 T2a = -(p >> 14), T2x = -tz((double)A2 * a * (double)a / pow(2, 29) + 0.003);
        i64 T1 = s * tz((double)A1 * a / 2048 + 0.02), T3 = -s * tz((double)A3 * a * (double)a * a / pow(2, 40) + 0.0045);
        if (C0 + T1 + T2a + T3 != V) { miss++; if (shown++ < 8) printf("t=%d V=%lld W=%lld T2arr=%lld T2abs=%lld exact2=%.4f\n", t, (long long)V, (long long)(C0 + T1 + T2a + T3), (long long)T2a, (long long)T2x, (double)A2 * a * (double)a / pow(2, 29)); } }
    printf("node1 misses %d/%d\n", miss, tot); return 0; }
