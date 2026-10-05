// Patent-guided square term at node 16 (C1 = C3 = 0): A = dx^2 (FM0), y5 = K2c * A (FM2), T2 = y5 * sin (FM2), V = C0 - T2.
#include "fsca_common.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
struct Smp { int t; i64 lo, hi; };
int main() { Fsca F; load_fsca(F); const i64 K = 1ll << 19, C0 = 67108863; std::vector<Smp> s;
    for (int t = -512; t <= 511; t++) { if (!t) continue; Win w = decode(F.srz[16384 + t], F.srn[16384 + t]); s.push_back({ t, C0 - w.hi, C0 - w.lo }); }   // T2 must lie in [lo,hi]
    struct R { int viol, ka, g, k5, b, si, u; i64 kc; }; std::vector<R> res; const i64 Sv[3] = { 0x800000, 0x7fffff, 0xffffff };
    std::vector<std::array<int,5>> cfg; for (int ka = 0; ka < 2; ka++) for (int g = 0; g <= 6; g++) for (int k5 = 0; k5 < 2; k5++) for (int b = 14; b <= 26; b += 2) for (int si = 0; si < 3; si++) cfg.push_back({ ka, g, k5, b, si });
    res.resize(cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t c = 0; c < cfg.size(); c++) { int ka = cfg[c][0], g = cfg[c][1], k5 = cfg[c][2], b = cfg[c][3], si = cfg[c][4]; R best = { 1 << 30, ka, g, k5, b, si, 0, 0 };
        std::vector<i64> Af(s.size()); for (size_t j = 0; j < s.size(); j++) { i64 tf = (i64)s[j].t << 13; i64 MA = (arr(tf, tf) + (ka ? K : 0)) >> 20; Af[j] = (MA >> g) & 0xffffff; }
        for (i64 kc = 0x4EF145 - 1600; kc <= 0x4EF145 + 1600; kc++) { std::vector<i64> prod(s.size()); double lr = 0; int cnt = 0; bool bad = false;
            for (size_t j = 0; j < s.size(); j++) { i64 y5 = (arr(kc, Af[j]) + (k5 ? K : 0)) >> b; if (y5 >= (1ll << 24)) { bad = true; break; } prod[j] = arr(y5, Sv[si]) + K; double mid = 0.5 * (s[j].lo + s[j].hi) + 0.5; if (mid > 300) { lr += log2((double)prod[j] / mid); cnt++; } }
            if (bad) break; int u = (int)llround(lr / cnt); if (u < 16 || u > 44) continue; int v = 0; for (size_t j = 0; j < s.size(); j++) { i64 T2 = sm(prod[j], u); if (T2 < s[j].lo || T2 > s[j].hi) v++; } if (v < best.viol) { best.viol = v; best.u = u; best.kc = kc; } }
        res[c] = best; }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.viol < y.viol; });
    printf("viol KA g K5  b  S       u   K2c (hex, offset from 0x4EF145)\n"); for (size_t i = 0; i < res.size() && i < 30; i++) printf("%4d  %d  %d  %d  %2d %06llx %2d  %06llx (%+lld)\n", res[i].viol, res[i].ka, res[i].g, res[i].k5, res[i].b, (long long)Sv[res[i].si], res[i].u, (long long)res[i].kc, (long long)(res[i].kc - 0x4EF145));
    return 0; }
