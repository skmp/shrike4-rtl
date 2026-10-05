// Combined test: FIPR array (mult28, constant g_K) for each product, sign-magnitude, nodes 0..2 both sides (exact region).
#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
struct ND { int n; i64 C0, A1, A2, A3; int s1, s2, s3; std::vector<int> ts; std::vector<i64> Vs; };
static ND nd[3];
static void load() { Fsca F; load_fsca(F); i64 A1[3] = { 0xc90fda, 0xc81800, 0xc532d6 }, A2[3] = { 0, 0xf79b60, 0x1ecd432 }, A3[3] = { 0xa56000, 0xa49008, 0xa22fed }, C0[3] = { 0, 6577819, 13092290 };
    for (int n = 0; n < 3; n++) { nd[n].n = n; nd[n].C0 = C0[n]; nd[n].A1 = A1[n]; nd[n].A2 = A2[n]; nd[n].A3 = A3[n]; nd[n].s1 = 1; nd[n].s2 = -1; nd[n].s3 = -1; for (int t = -512; t <= 511; t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; nd[n].ts.push_back(t); nd[n].Vs.push_back(w.sign * w.lo); } } }
// abstract products (exact + magnitude constant), used for the products not under test
static inline i64 absT(i64 A, double pw_signed, double c, int sgnC, double scale) { double m = (double)A * fabs(pw_signed) * scale; i64 v = tz(m + c); int s = (pw_signed < 0 ? -1 : 1) * sgnC; return s * v; }
int main() { load();
    // ---- linear through the array: rec = |t|<<15 (or C1 as rec), mul = C1; T1 = sign * (p28 >> 6)
    printf("LINEAR via array (misses per node 0/1/2, total) for K values; square/cubic abstract (c2 0.003, c3 0.0045):\n");
    for (int rec = 1; rec >= 0; rec--) { printf(" recoded operand = %s\n", rec ? "|t|<<15" : "C1"); std::vector<std::pair<int, i64>> best;
        for (i64 K = 0; K <= (3ll << 20); K += 1 << 16) { g_K = K; int m[3] = {0,0,0}; for (int q = 0; q < 3; q++) { ND& d = nd[q]; for (size_t j = 0; j < d.ts.size(); j++) { int t = d.ts[j]; int a = t < 0 ? -t : t, s = t < 0 ? -1 : 1; i64 p = rec ? mult28((uint32_t)(a << 15), (uint32_t)d.A1) : mult28((uint32_t)d.A1, (uint32_t)(a << 15)); i64 T1 = s * (p >> 6);
                i64 T2 = absT(d.A2, (double)t * t, 0.003, d.s2, 1.0 / pow(2, 29)), T3 = absT(d.A3, (double)t * t * t, 0.0045, d.s3, 1.0 / pow(2, 40)); if (d.C0 + T1 + T2 + T3 != d.Vs[j]) m[q]++; } }
            best.push_back({ m[0] + m[1] + m[2], K }); if (m[0] + m[1] + m[2] <= 12) printf("  K=%8lld (%.4f u): %d/%d/%d total %d\n", (long long)K, (double)K / (1 << 26), m[0], m[1], m[2], m[0] + m[1] + m[2]); }
        std::sort(best.begin(), best.end()); printf("  best total %d at K=%lld\n", best[0].first, (long long)best[0].second); }
    return 0; }
