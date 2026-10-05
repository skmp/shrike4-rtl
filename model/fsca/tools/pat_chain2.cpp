// FSCA square term as a coefficient-first chain, FSRRA style:  I = ((arr(C2f, dx) or arr(dx, C2f)) + KI) >> (20+gi)  [floor or sm]
//                                                             T2 = sm((arr(dx, I) or arr(I, dx)) + K, u)
// Nodes 1, 2 (exact data, cubic as solved) and 16 (pure square, windows). C2f scanned per node.
#include "fsca_common.h"
#include <vector>
#include <array>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19;
struct Node { int n; i64 C0, M1; int e1; i64 C2f; int e2; i64 C3; };
int main() { Fsca F; load_fsca(F); Node nd[3] = { { 1, 6577819, 0xc81800, 0, 0x7bcdaa, 0, 0xa49 }, { 2, 13092290, 0xc532d6, 0, 0x7b350e, 1, 0xa23 }, { 16, 67108863, 0, 0, 0x4ef144, 4, 0 } };
    std::vector<i64> lo[3], hi[3]; for (int q = 0; q < 3; q++) for (int t = -512; t <= 511; t++) { int idx = nd[q].n * 1024 + t; Win w = decode(F.srz[idx], F.srn[idx]); i64 dx = (i64)t << 13; i64 T1 = nd[q].M1 ? sm(arr(nd[q].M1 >> 1, dx) + K, 23 - nd[q].e1) : 0; i64 T3 = 0; if (nd[q].C3) { i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; T3 = sm(arr(nd[q].C3 << 5, A3) + K, 26); } i64 base = nd[q].C0 + T1 - T3; lo[q].push_back(base - w.hi); hi[q].push_back(base - w.lo); }
    const i64 KIs[8] = { 0, 1ll << 17, 1ll << 18, 1ll << 19, (1ll << 19) + (1ll << 16), (1ll << 19) + (1ll << 17), (1ll << 19) + (1ll << 18), 1ll << 20 };
    std::vector<std::array<int,5>> cfg; for (int r1 = 0; r1 < 2; r1++) for (int gi = 1; gi <= 9; gi++) for (int ki = 0; ki < 8; ki++) for (int im = 0; im < 2; im++) for (int r2 = 0; r2 < 2; r2++) cfg.push_back({ r1, gi, ki, im, r2 });
    struct R { int tot, m[3]; std::array<int,5> c; i64 cf[3]; }; std::vector<R> res(cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t ci = 0; ci < cfg.size(); ci++) { int r1 = cfg[ci][0], gi = cfg[ci][1], ki = cfg[ci][2], im = cfg[ci][3], r2 = cfg[ci][4]; R r; r.c = cfg[ci]; r.tot = 0;
        for (int q = 0; q < 3; q++) { int best = 1 << 30; i64 bc = 0; int u = 34 - gi - nd[q].e2;   // C2 = (C2f<<1)*2^e2 [2^-55]; I ~ C2f*t*2^13/2^(20+gi); I*t*2^13 = C2f t^2 2^(6-gi) ; T2 = C2f 2^(1+e2) t^2 / 2^29 -> u = 34 - gi - e2
            for (i64 c2 = nd[q].C2f - 8; c2 <= nd[q].C2f + 8; c2++) { int miss = 0; for (int j = 0; j < 1024 && miss < best; j++) { i64 t = j - 512; i64 dx = t << 13; i64 PI = (r1 == 0 ? arr(c2, dx) : arr(dx, c2)) + KIs[ki]; i64 I = im == 0 ? (PI >> (20 + gi)) : sm(PI, 20 + gi); if (I >= (1ll << 25) || I < -(1ll << 25)) { miss = 1 << 29; break; } i64 P2 = (r2 == 0 ? arr(dx, I) : arr(I, dx)) + K; i64 T2 = sm(P2, u); if (T2 < lo[q][j] || T2 > hi[q][j]) miss++; } if (miss < best) { best = miss; bc = c2; } }
            r.m[q] = best; r.cf[q] = bc; r.tot += best; if (r.tot > (1 << 28)) break; }
        res[ci] = r; }
    std::sort(res.begin(), res.end(), [](const R& a, const R& b) { return a.tot < b.tot; });
    printf("total (n1 n2 n16) | rolesI gi KI/2^16 Imode rolesT | C2 fields\n"); for (int i = 0; i < 25; i++) printf("%4d (%d %d %d) | %d %d %3lld %d %d | %06llx %06llx %06llx\n", res[i].tot, res[i].m[0], res[i].m[1], res[i].m[2], res[i].c[0], res[i].c[1], (long long)(KIs[res[i].c[2]] >> 16), res[i].c[3], res[i].c[4], (long long)res[i].cf[0], (long long)res[i].cf[1], (long long)res[i].cf[2]);
    return 0; }
