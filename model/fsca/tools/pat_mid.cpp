// Square chain variants scored on the middle nodes 3..15 (window violations), nodes 1,2,16 as a guard.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19, KC = 0x4ef145;
static const i64 M1[17] = { 0xc90fda, 0xc81800, 0xc532d6, 0xc0677e, 0xb9c1ca, 0xb15220, 0xa72d48, 0x9b6c48, 0x8e2c18, 0xff1acc, 0xdf6876, 0xbd8f54, 0x99e2da, 0xe975f6, 0x9ce6a4, 0x9da8fe, 0 };
static const int E1[17] = { 0,0,0,0,0,0,0,0,0,-1,-1,-1,-1,-2,-2,-3, 0 };
static const i64 C3g[17] = { 0xa56, 0xa49, 0xa23, 0x9e4, 0x98c, 0x91d, 0x898, 0x7fd, 0x74f, 0x68f, 0x5be, 0x4df, 0x3f5, 0x300, 0x204, 0x103, 0 };
int main() { Fsca F; load_fsca(F); std::vector<i64> lo[17], hi[17];
    for (int n = 1; n <= 16; n++) for (int t = -512; t <= 511; t++) { Win w = decode(F.srz[n * 1024 + t], F.srn[n * 1024 + t]); i64 dx = (i64)t << 13; i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; i64 base = FSCA_TAB[n][0] + (M1[n] ? sm(arr(M1[n] >> 1, dx) + K, 23 - E1[n]) : 0) - (C3g[n] ? sm(arr(C3g[n] << 5, A3) + K, 26) : 0); lo[n].push_back(base - w.hi); hi[n].push_back(base - w.lo); }
    // config: order (0: K d d S, 1: K d S d, 2: K S d d, 3: S d K d ... keep the first two + S-first variants), roles 0..7, g1, g2, sine width (23/24), sine rounding (RN/trunc)
    std::vector<std::array<int,6>> cfg; for (int order = 0; order < 4; order++) for (int roles = 0; roles < 8; roles++) for (int g1 = 2; g1 <= 4; g1++) for (int g2 = 0; g2 <= 4; g2++) for (int sw = 23; sw <= 24; sw++) for (int sr = 0; sr < 2; sr++) cfg.push_back({ order, roles, g1, g2, sw, sr });
    struct R { int mid, guard; std::array<int,6> c; int per[17]; }; std::vector<R> res(cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t ci = 0; ci < cfg.size(); ci++) { auto c = cfg[ci]; R r; r.c = c; r.mid = 0; r.guard = 0;
        for (int n = 1; n <= 16; n++) { double sv = sin(n * M_PI / 32); int e = 0; while (sv * pow(2, e) < 0.5) e++; double mant = sv * pow(2, e) * pow(2, c[4]); i64 S = c[5] ? (i64)floor(mant) : (i64)llround(mant); if (S >= (1ll << c[4])) S = (1ll << c[4]) - 1;
            auto prod = [&](int t) { i64 d = (i64)t << 13; i64 I; if (c[0] == 0) { I = (((c[1] & 1) ? arr(d, KC) : arr(KC, d)) + K) >> (20 + c[2]); I = (((c[1] & 2) ? arr(d, I) : arr(I, d)) + K) >> (20 + c[3]); return ((c[1] & 4) ? arr(S, I) : arr(I, S)) + K; }
                if (c[0] == 1) { I = (((c[1] & 1) ? arr(d, KC) : arr(KC, d)) + K) >> (20 + c[2]); I = (((c[1] & 2) ? arr(S, I) : arr(I, S)) + K) >> (20 + c[3]); return ((c[1] & 4) ? arr(d, I) : arr(I, d)) + K; }
                if (c[0] == 2) { I = (((c[1] & 1) ? arr(S, KC) : arr(KC, S)) + K) >> (20 + c[2]); I = (((c[1] & 2) ? arr(d, I) : arr(I, d)) + K) >> (20 + c[3]); return ((c[1] & 4) ? arr(d, I) : arr(I, d)) + K; }
                I = (((c[1] & 1) ? arr(d, S) : arr(S, d)) + K) >> (20 + c[2]); I = (((c[1] & 2) ? arr(KC, I) : arr(I, KC)) + K) >> (20 + c[3]); return ((c[1] & 4) ? arr(d, I) : arr(I, d)) + K; };
            double lr = 0; int cnt = 0; bool bad = false; for (int t = 200; t <= 511; t += 7) { i64 p = prod(t); if (p <= 0) { bad = true; break; } double ex = fabs((double)FSCA_TAB[n][2]) * t * (double)t / 536870912.0; lr += log2((double)p / ex); cnt++; } if (bad) { r.mid = 1 << 29; break; } int u = (int)llround(lr / cnt); if (u < 16 || u > 50) { r.mid = 1 << 29; break; }
            int miss = 0; for (int j = 0; j < 1024; j++) { i64 T2 = sm(prod(j - 512), u); if (T2 < lo[n][j] || T2 > hi[n][j]) miss++; } r.per[n] = miss; if (n <= 2 || n == 16) r.guard += miss; else r.mid += miss; }
        res[ci] = r; }
    std::sort(res.begin(), res.end(), [](const R& a, const R& b) { return a.mid + 10 * a.guard < b.mid + 10 * b.guard; }); const char* on[4] = { "K*d*d*S", "K*d*S*d", "K*S*d*d", "S*d*K*d" };
    printf("mid guard | order roles g1 g2 Swidth Sround | per node 1..16\n"); for (int i = 0; i < 20; i++) { printf("%4d %3d | %s %d %d %d %d %s |", res[i].mid, res[i].guard, on[res[i].c[0]], res[i].c[1], res[i].c[2], res[i].c[3], res[i].c[4], res[i].c[5] ? "trunc" : "RN"); for (int n = 1; n <= 16; n++) printf(" %d", res[i].per[n]); printf("\n"); }
    return 0; }
