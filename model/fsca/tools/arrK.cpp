#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline i64 tzi(i64 x, int sh) { return x >= 0 ? (x >> sh) : -((-x) >> sh); }
static inline i64 arr_signed(uint32_t rec24, uint32_t mul) { u64 R[13]; i64 T = 0; uint32_t x = rec24 & 0xffffff; if (x & 0x800000) x |= 0x3000000;
    for (int k = 0; k < 13; k++) { int d = booth_digit(x, k); u64 mag = (u64)(d < 0 ? -d : d) * mul; u64 oc = d < 0 ? (~mag & ((1ull << 25) - 1)) : mag; R[k] = oc << (2*k); T += (i64)((R[k] >> 16) << 16); if (d < 0) { if (2*k >= 18) T += 1ll << (2*k); T -= 1ll << (25 + 2*k); } }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
int main() { Fsca F; load_fsca(F); i64 A1[3] = { 0xc90fda, 0xc81800, 0xc532d6 }, A2[3] = { 0, 0xf79b60, 0x1ecd432 }, A3[3] = { 0xa56000, 0xa49008, 0xa22fed }, C0[3] = { 0, 6577819, 13092290 };
    // for each node/side: the K interval [Klo,Khi) with zero misses = intersection over samples of the allowed K interval for T1 (given V, C0, T2, T3 exact-abstract)
    for (int n = 0; n < 3; n++) for (int side = 0; side < 2; side++) { i64 Klo = -(1ll << 40), Khi = (1ll << 40); int cnt = 0, viol = 0; std::vector<std::pair<i64,i64>> iv;
        for (int t = side ? 1 : -512; t <= (side ? 511 : -1); t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; cnt++; i64 V = w.sign * w.lo; int a = t < 0 ? -t : t, s = t < 0 ? -1 : 1;
            i64 T2 = -tz((double)A2[n] * a * (double)a / pow(2, 29) + 0.003), T3 = -tz((double)A3[n] * t * (double)t * t / pow(2, 40) + 0.0045); i64 T1 = V - C0[n] - T2 - T3; i64 T = arr_signed((uint32_t)(t << 14), (uint32_t)A1[n]);
            // need tzi(T+K,25) == T1: for T1>=0: T1*2^25 <= T+K < (T1+1)*2^25 ; for T1<0: -(|T1|+1)*2^25 < T+K <= -|T1|*2^25
            i64 lo, hi; if (T1 >= 0) { lo = (T1 << 25) - T; hi = ((T1 + 1) << 25) - T; } else { lo = -((-T1 + 1) << 25) - T + 1; hi = -((-T1) << 25) - T + 1; }
            iv.push_back({lo, hi}); }
        // find K maximizing coverage
        std::vector<std::pair<i64,int>> ev; for (auto& p : iv) { ev.push_back({p.first, +1}); ev.push_back({p.second, -1}); } std::sort(ev.begin(), ev.end()); int cur = 0, best = 0; i64 bk = 0, bk2 = 0; for (size_t e = 0; e < ev.size(); e++) { cur += ev[e].second; if (cur > best) { best = cur; bk = ev[e].first; bk2 = e + 1 < ev.size() ? ev[e + 1].first : bk; } }
        printf("node %d side %c: samples %d, best coverage %d (misses %d) for K in [%lld, %lld) = [%.4f, %.4f) u\n", n, side ? '+' : '-', cnt, best, cnt - best, (long long)bk, (long long)bk2, bk / 33554432.0, bk2 / 33554432.0); }
    return 0; }
