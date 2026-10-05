// Per-node fit of the second latch shift g2 (I2 = (arr(I1,dx)+K) >> (20+g2)) and of a shift applied to the sine operand, S fitted.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <array>
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19, KC = 0x4ef145;
static const i64 M1[17] = { 0xc90fda, 0xc81800, 0xc532d6, 0xc0677e, 0xb9c1ca, 0xb15220, 0xa72d48, 0x9b6c48, 0x8e2c18, 0xff1acc, 0xdf6876, 0xbd8f54, 0x99e2da, 0xe975f6, 0x9ce6a4, 0x9da8fe, 0 };
static const int E1[17] = { 0,0,0,0,0,0,0,0,0,-1,-1,-1,-1,-2,-2,-3, 0 };
static const i64 C3g[17] = { 0xa56, 0xa49, 0xa23, 0x9e4, 0x98c, 0x91d, 0x898, 0x7fd, 0x74f, 0x68f, 0x5be, 0x4df, 0x3f5, 0x300, 0x204, 0x103, 0 };
// Patent-style cubic in chain form: Y1 = (arr(K3, dx)+K)>>(20+g1) ; Y2 = (arr(Y1, dx)+K)>>(20+g2) ; L = linear product latch P1>>(20+gl) (signed)
// T3 = sm(arr(Y2, L) or arr(L, Y2) + K, u).  K3 global (scanned), guard nodes 0, 1, 2 (all exact data).
static std::vector<i64> lo[17], hi[17], T2v[17], T1v[17], P1v[17];
int main() { Fsca F; load_fsca(F); std::vector<i64> I1v(1024), I2v(1024); for (int j = 0; j < 1024; j++) { i64 d = (i64)(j - 512) << 13; I1v[j] = (arr(KC, d) + K) >> 23; I2v[j] = (arr(I1v[j], d) + K) >> 20; }
    for (int n = 0; n <= 2; n++) { double sv = sin(n * M_PI / 32); int e = 0; if (n) while (sv * pow(2, e) < 0.5) e++; i64 S = n ? (i64)llround(sv * pow(2, e + 23)) : 0; int u = n == 1 ? 33 : 32;
        for (int j = 0; j < 1024; j++) { int t = j - 512; Win w = decode(F.srz[(n * 1024 + t + 65536) % 65536], F.srn[(n * 1024 + t + 65536) % 65536]); i64 a = (i64)w.sign * w.lo, b = (i64)w.sign * w.hi; if (a > b) std::swap(a, b); lo[n].push_back(a); hi[n].push_back(b); T2v[n].push_back(n ? sm(arr(I2v[j], S) + K, u) : 0); i64 p = arr(M1[n] >> 1, (i64)t << 13) + K; P1v[n].push_back(p); T1v[n].push_back(sm(p, 23)); } }
    struct Cfg { int g1, g2, gl, roles, lmode; }; std::vector<Cfg> cfg; for (int g1 = 0; g1 <= 6; g1++) for (int g2 = 0; g2 <= 6; g2++) for (int gl = 0; gl <= 8; gl++) for (int roles = 0; roles < 2; roles++) for (int lm = 0; lm < 2; lm++) cfg.push_back({ g1, g2, gl, roles, lm });
    struct R { Cfg c; int tot, m[3]; i64 k3; int u; }; std::vector<R> res(cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t ci = 0; ci < cfg.size(); ci++) { Cfg c = cfg[ci]; R best; best.c = c; best.tot = 1 << 29;
        for (i64 k3 = 0x694699 - 1500; k3 <= 0x694699 + 1500; k3 += 1) { std::vector<i64> Y2(1024); bool bad = false; for (int j = 0; j < 1024; j++) { i64 d = (i64)(j - 512) << 13; i64 Y1 = (arr(k3, d) + K) >> (20 + c.g1); Y2[j] = (arr(Y1, d) + K) >> (20 + c.g2); if (Y2[j] >= (1ll << 25)) { bad = true; break; } } if (bad) break;
            R r; r.c = c; r.k3 = k3; r.tot = 0; int u = -1; for (int n = 0; n <= 2; n++) { std::vector<i64> P(1024); for (int j = 0; j < 1024; j++) { i64 L = c.lmode ? sm(P1v[n][j], 20 + c.gl) : (P1v[n][j] >> (20 + c.gl)); P[j] = (c.roles ? arr(L, Y2[j]) : arr(Y2[j], L)) + K; }
                if (u < 0) { double ex = (double)(C3g[0] << 12) * 400.0 * 400.0 * 400.0 / 1099511627776.0; if (P[912] <= 0) { bad = true; break; } u = (int)llround(log2((double)P[912] / ex)); if (u < 16 || u > 44) { bad = true; break; } }
                int miss = 0; for (int j = 0; j < 1024 && r.tot + miss < best.tot; j++) { i64 V = FSCA_TAB[n][0] + T1v[n][j] - T2v[n][j] - sm(P[j], u); if (V < lo[n][j] || V > hi[n][j]) miss++; } r.m[n] = miss; r.tot += miss; if (r.tot >= best.tot) break; }
            if (!bad && r.tot < best.tot) { best = r; best.u = u; } }
        res[ci] = best; }
    std::sort(res.begin(), res.end(), [](const R& a, const R& b) { return a.tot < b.tot; }); printf("total (n0 n1 n2) | g1 g2 gl roles Lmode u | K3 offset from 2pi^2/3\n"); for (int i = 0; i < 15; i++) printf("%5d (%d %d %d) | %d %d %d %d %d %2d | %+lld\n", res[i].tot, res[i].m[0], res[i].m[1], res[i].m[2], res[i].c.g1, res[i].c.g2, res[i].c.gl, res[i].c.roles, res[i].c.lmode, res[i].u, (long long)(res[i].k3 - 0x694699));
    return 0; }
