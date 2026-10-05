// Patent square chain on nodes 1, 2 (exact) and 16: y5 = (arr(K2c, t^2<<g) + K5) >> b ; T2 = sm(arr(y5, Sf) + K, u) (or roles swapped)
// Sf = 24-bit normalised mantissa of the table sine (taken from C0); K2c shared.
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
static const i64 K = 1ll << 19;
struct Node { int n; i64 C0, M1; int e1; i64 C3; double c2; };
int main() { Fsca F; load_fsca(F); Node nd[3] = { { 1, 6577819, 0xc81800, 0, 0xa49, 16227180.0 }, { 2, 13092290, 0xc532d6, 0, 0xa23, 32298060.0 }, { 16, 67108863, 0, 0, 0, 165554336.0 } };
    // precompute the non-square part and windows
    std::vector<i64> lo[3], hi[3]; for (int q = 0; q < 3; q++) for (int t = -512; t <= 511; t++) { int idx = nd[q].n * 1024 + t; Win w = decode(F.srz[idx], F.srn[idx]); i64 dx = (i64)t << 13; i64 T1 = nd[q].M1 ? sm(arr(nd[q].M1 >> 1, dx) + K, 23 - nd[q].e1) : 0; i64 T3 = 0; if (nd[q].C3) { i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; T3 = sm(arr(nd[q].C3 << 5, A3) + K, 26); } i64 base = nd[q].C0 + T1 - T3; lo[q].push_back(base - w.hi); hi[q].push_back(base - w.lo); }  // T2 in [lo,hi]
    i64 Sf[3][2]; for (int q = 0; q < 3; q++) { i64 m = nd[q].C0; while (m >= (1ll << 24)) m >>= 1; while (m < (1ll << 23)) m <<= 1; Sf[q][0] = m; Sf[q][1] = m >> 1; } Sf[2][0] = 0xffffff; Sf[2][1] = 0x7fffff;
    struct R { int tot, m[3], g, b, k5, dAp, dAn, dyp, dyn; i64 kc; }; std::vector<std::array<int,7>> cfg;
    for (int g : {1, 3, 4, 6}) for (int b = 18; b <= 24; b++) for (int k5 = 0; k5 < 2; k5++) for (int dAp = -1; dAp <= 1; dAp++) for (int dAn = -1; dAn <= 1; dAn++) for (int dyp = -2; dyp <= 2; dyp++) for (int dyn = -2; dyn <= 2; dyn++) cfg.push_back({ g, b, k5, dAp, dAn, dyp, dyn }); std::vector<R> res(cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t c = 0; c < cfg.size(); c++) { int g = cfg[c][0], b = cfg[c][1], k5 = cfg[c][2], dAp = cfg[c][3], dAn = cfg[c][4], dyp = cfg[c][5], dyn = cfg[c][6]; R best; best.tot = 1 << 30; best.g = g; best.b = b; best.k5 = k5; best.dAp = dAp; best.dAn = dAn; best.dyp = dyp; best.dyn = dyn;
        for (i64 kc = 0x4ef144 - 10; kc <= 0x4ef144 + 10; kc++) { R r = best; r.kc = kc; r.tot = 0; bool bad = false;
            for (int q = 0; q < 3 && !bad; q++) { std::vector<i64> P(1024); double lr = 0; int cnt = 0; for (int j = 0; j < 1024; j++) { i64 t = j - 512; i64 A = ((t * t) << g) + (t > 0 ? dAp : t < 0 ? dAn : 0); if (A < 0) A = 0; i64 y5 = ((arr(kc, A) + (k5 ? K : 0)) >> b) + (t > 0 ? dyp : t < 0 ? dyn : 0); if (y5 < 0) y5 = 0; if (y5 >= (1ll << 24)) { bad = true; break; } P[j] = arr(y5, Sf[q][0]) + K; double ex = nd[q].c2 * t * (double)t / 536870912.0; if (ex > 200) { lr += log2((double)P[j] / ex); cnt++; } }
                if (bad) break; int u = (int)llround(lr / cnt); int m = 0; for (int j = 0; j < 1024; j++) { i64 T2 = sm(P[j], u); if (T2 < lo[q][j] || T2 > hi[q][j]) m++; } r.m[q] = m; r.tot += m; }
            if (!bad && r.tot < best.tot) best = r; }
        res[c] = best; }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.tot < y.tot; });
    printf("total (n1 n2 n16) | g  b K5 | dA+ dA- dy+ dy- | K2c\n"); for (int i = 0; i < 30; i++) printf("%4d (%d %d %d) | %d %2d %d | %+d %+d %+d %+d | %06llx\n", res[i].tot, res[i].m[0], res[i].m[1], res[i].m[2], res[i].g, res[i].b, res[i].k5, res[i].dAp, res[i].dAn, res[i].dyp, res[i].dyn, (long long)res[i].kc);
    return 0; }
