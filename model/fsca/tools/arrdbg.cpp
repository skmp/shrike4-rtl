#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; load_fsca(F); g_K = 5ll << 19; int m = 0; for (int t = -511; t <= 511; t++) { if (!t) continue; int i = (t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); i64 V = w.sign * w.lo; int a = t < 0 ? -t : t, s = t < 0 ? -1 : 1;
    i64 p = mult28((uint32_t)(a << 15), 0xc90fdau); i64 T1 = s * (p >> 6); double e3 = (double)0xa56000 * a * (double)a * a / pow(2, 40); i64 T3 = -s * tz(e3 + 0.0045); i64 W = T1 + T3; if (W != V) { m++; if (m <= 6) printf("t=%d V=%lld W=%lld T1=%lld (p=%lld) T3=%lld  exact e1=%.4f\n", t, (long long)V, (long long)W, (long long)T1, (long long)p, (long long)T3, (double)0xc90fda * a / 2048); } }
    printf("node 0 misses at K=5*2^19: %d of 1022\n", m); return 0; }
