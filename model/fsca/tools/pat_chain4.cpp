// All multiplication orders of {Kc (global constant), S (table sine mantissa), d, d} as a left-to-right chain through the array.
// Each intermediate: I = (arr(rec, mul) + K) >> (20 + g), floor. Final: sm(P, u), u from the scale. Nodes 1, 2 (exact) and 16.
#include "fsca_common.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19;
struct Node { int n; i64 C0, M1; int e1; i64 C3; double c2; i64 S; };
int main() { Fsca F; load_fsca(F); Node nd[3] = { { 1, 6577819, 0xc81800, 0, 0xa49, 16227158.0, 0 }, { 2, 13092290, 0xc532d6, 0, 0xa23, 32298045.0, 0 }, { 16, 67108863, 0, 0, 0, 165554336.0, 0 } };
    std::vector<i64> lo[3], hi[3]; for (int q = 0; q < 3; q++) { i64 m = nd[q].C0; while (m >= (1ll << 24)) m >>= 1; while (m < (1ll << 23)) m <<= 1; nd[q].S = m; for (int t = -512; t <= 511; t++) { int idx = nd[q].n * 1024 + t; Win w = decode(F.srz[idx], F.srn[idx]); i64 dx = (i64)t << 13; i64 T1 = nd[q].M1 ? sm(arr(nd[q].M1 >> 1, dx) + K, 23 - nd[q].e1) : 0; i64 T3 = 0; if (nd[q].C3) { i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; T3 = sm(arr(nd[q].C3 << 5, A3) + K, 26); } i64 base = nd[q].C0 + T1 - T3; lo[q].push_back(base - w.hi); hi[q].push_back(base - w.lo); } }
    // sequences: operand codes 0 = Kc, 1 = S, 2 = d
    std::vector<std::array<int,4>> seqs = { {0,2,2,1}, {0,2,1,2}, {0,1,2,2}, {1,2,2,0}, {1,2,0,2}, {1,0,2,2}, {2,2,0,1}, {2,2,1,0}, {2,0,2,1}, {2,0,1,2}, {2,1,2,0}, {2,1,0,2} };
    struct Cfg { int s, g1, g2, roles, sshift; }; std::vector<Cfg> cfg; for (int s = 0; s < (int)seqs.size(); s++) for (int g1 = 0; g1 <= 7; g1++) for (int g2 = 0; g2 <= 7; g2++) for (int roles = 0; roles < 8; roles++) for (int ss = 0; ss < 2; ss++) cfg.push_back({ s, g1, g2, roles, ss });
    struct R { int tot, m[3]; Cfg c; i64 kc; }; std::vector<R> res(cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t ci = 0; ci < cfg.size(); ci++) { Cfg c = cfg[ci]; R best; best.tot = 1 << 30; best.c = c; best.kc = 0;
        for (i64 kc = 0x4ef142; kc <= 0x4ef148; kc++) { R r; r.c = c; r.kc = kc; r.tot = 0; bool bad = false;
            for (int q = 0; q < 3 && !bad; q++) { i64 S = nd[q].S >> c.sshift; if (q == 2) S = c.sshift ? 0x7fffff : 0xffffff; std::vector<i64> P(1024); double lr = 0; int cnt = 0;
                for (int j = 0; j < 1024; j++) { i64 t = j - 512; i64 d = t << 13; auto val = [&](int code) { return code == 0 ? kc : code == 1 ? S : d; }; i64 a = val(seqs[c.s][0]), b = val(seqs[c.s][1]); i64 p = ((c.roles & 1) ? arr(b, a) : arr(a, b)) + K; i64 I = p >> (20 + c.g1); if (std::llabs(I) >= (1ll << 25)) { bad = true; break; }
                    b = val(seqs[c.s][2]); p = ((c.roles & 2) ? arr(b, I) : arr(I, b)) + K; I = p >> (20 + c.g2); if (std::llabs(I) >= (1ll << 25)) { bad = true; break; } b = val(seqs[c.s][3]); P[j] = ((c.roles & 4) ? arr(b, I) : arr(I, b)) + K; double ex = nd[q].c2 * t * (double)t / 536870912.0; if (ex > 300) { if (P[j] <= 0) { bad = true; break; } lr += log2((double)P[j] / ex); cnt++; } }
                if (bad || !cnt) { bad = true; break; } int u = (int)llround(lr / cnt); if (u < 16 || u > 46) { bad = true; break; } int miss = 0; for (int j = 0; j < 1024; j++) { i64 T2 = sm(P[j], u); if (T2 < lo[q][j] || T2 > hi[q][j]) miss++; } r.m[q] = miss; r.tot += miss; if (r.tot >= best.tot) break; }
            if (!bad && r.tot < best.tot) best = r; }
        res[ci] = best; }
    std::sort(res.begin(), res.end(), [](const R& a, const R& b) { return a.tot < b.tot; }); const char* nm = "KSd";
    printf("total (n1 n2 n16) | order g1 g2 roles Sshift | Kc\n"); for (int i = 0; i < 24; i++) { auto& s = seqs[res[i].c.s]; printf("%4d (%d %d %d) | %c*%c*%c*%c  %d %d  %d  %d | %06llx\n", res[i].tot, res[i].m[0], res[i].m[1], res[i].m[2], nm[s[0]], nm[s[1]], nm[s[2]], nm[s[3]], res[i].c.g1, res[i].c.g2, res[i].c.roles, res[i].c.sshift, (long long)res[i].kc); }
    return 0; }
