// Complete enumeration: all 12 orders of {Kc,S,d,d}, latch shifts g1,g2 in 0..7, 8 role combinations, two sine formats.
// Guard: nodes 1, 2, 16 must be exact. Score: window violations on nodes 3..15.
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
static const i64 K = 1ll << 19;
static const i64 M1[17] = { 0xc90fda, 0xc81800, 0xc532d6, 0xc0677e, 0xb9c1ca, 0xb15220, 0xa72d48, 0x9b6c48, 0x8e2c18, 0xff1acc, 0xdf6876, 0xbd8f54, 0x99e2da, 0xe975f6, 0x9ce6a4, 0x9da8fe, 0 };
static const int E1[17] = { 0,0,0,0,0,0,0,0,0,-1,-1,-1,-1,-2,-2,-3, 0 };
static const i64 C3g[17] = { 0xa56, 0xa49, 0xa23, 0x9e4, 0x98c, 0x91d, 0x898, 0x7fd, 0x74f, 0x68f, 0x5be, 0x4df, 0x3f5, 0x300, 0x204, 0x103, 0 };
int main() { Fsca F; load_fsca(F); std::vector<i64> lo[17], hi[17]; i64 Sv[17];
    for (int n = 1; n <= 16; n++) { double sv = sin(n * M_PI / 32); int e = 0; while (sv * pow(2, e) < 0.5) e++; Sv[n] = std::min((i64)llround(sv * pow(2, e + 23)), (i64)((1ll << 23) - 1)); for (int t = -512; t <= 511; t++) { Win w = decode(F.srz[n * 1024 + t], F.srn[n * 1024 + t]); i64 dx = (i64)t << 13; i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; i64 base = FSCA_TAB[n][0] + (M1[n] ? sm(arr(M1[n] >> 1, dx) + K, 23 - E1[n]) : 0) - (C3g[n] ? sm(arr(C3g[n] << 5, A3) + K, 26) : 0); lo[n].push_back(base - w.hi); hi[n].push_back(base - w.lo); } }
    std::vector<std::array<int,4>> seqs = { {0,2,2,1}, {0,2,1,2}, {0,1,2,2}, {1,2,2,0}, {1,2,0,2}, {1,0,2,2}, {2,2,0,1}, {2,2,1,0}, {2,0,2,1}, {2,0,1,2}, {2,1,2,0}, {2,1,0,2} };
    struct Cfg { int s, g1, g2, roles; }; std::vector<Cfg> cfg; for (int s = 0; s < 12; s++) for (int g1 = 0; g1 <= 7; g1++) for (int g2 = 0; g2 <= 7; g2++) for (int roles = 0; roles < 8; roles++) cfg.push_back({ s, g1, g2, roles });
    struct R { int guard, mid; Cfg c; i64 kc; int per[17]; }; std::vector<R> res(cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t ci = 0; ci < cfg.size(); ci++) { Cfg c = cfg[ci]; R best; best.guard = 1 << 29; best.mid = 1 << 29; best.c = c; best.kc = 0;
        for (i64 kc = 0x4ef143; kc <= 0x4ef147; kc++) { R r; r.c = c; r.kc = kc; r.guard = 0; r.mid = 0; bool bad = false; const int order[16] = { 1, 2, 16, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
            for (int oi = 0; oi < 16 && !bad; oi++) { int n = order[oi]; i64 S = Sv[n]; auto prod = [&](int t, bool& ovf) { i64 d = (i64)t << 13; auto val = [&](int code) { return code == 0 ? kc : code == 1 ? S : d; }; i64 a = val(seqs[c.s][0]), b = val(seqs[c.s][1]); i64 I = (((c.roles & 1) ? arr(b, a) : arr(a, b)) + K) >> (20 + c.g1); if (std::llabs(I) >= (1ll << 25)) ovf = true; b = val(seqs[c.s][2]); I = (((c.roles & 2) ? arr(b, I) : arr(I, b)) + K) >> (20 + c.g2); if (std::llabs(I) >= (1ll << 25)) ovf = true; b = val(seqs[c.s][3]); return ((c.roles & 4) ? arr(b, I) : arr(I, b)) + K; };
                double lr = 0; int cnt = 0; bool ovf = false; for (int t = 200; t <= 511; t += 13) { i64 p = prod(t, ovf); if (p <= 0) { ovf = true; break; } lr += log2((double)p / (fabs((double)FSCA_TAB[n][2]) * t * (double)t / 536870912.0)); cnt++; } if (ovf) { bad = true; break; } int u = (int)llround(lr / cnt); if (u < 16 || u > 50) { bad = true; break; }
                int miss = 0; for (int j = 0; j < 1024; j++) { bool o2 = false; i64 T2 = sm(prod(j - 512, o2), u); if (T2 < lo[n][j] || T2 > hi[n][j]) miss++; } r.per[n] = miss; if (oi < 3) { r.guard += miss; if (r.guard > 0) { bad = true; break; } } else r.mid += miss; }
            if (!bad && r.mid < best.mid) best = r; }
        res[ci] = best; }
    std::sort(res.begin(), res.end(), [](const R& a, const R& b) { return a.mid < b.mid; }); const char* nm = "KSd"; int shown = 0;
    printf("mid | order g1 g2 roles Kc | per node 3..15\n"); for (auto& r : res) { if (r.guard != 0 || r.mid >= (1 << 28)) continue; auto& s = seqs[r.c.s]; printf("%4d | %c*%c*%c*%c %d %d %d %06llx |", r.mid, nm[s[0]], nm[s[1]], nm[s[2]], nm[s[3]], r.c.g1, r.c.g2, r.c.roles, (long long)r.kc); for (int n = 3; n <= 15; n++) printf(" %d", r.per[n]); printf("\n"); if (++shown >= 30) break; }
    printf("configs exact on the guard nodes: %d\n", (int)std::count_if(res.begin(), res.end(), [](const R& r) { return r.guard == 0 && r.mid < (1 << 28); })); return 0; }
