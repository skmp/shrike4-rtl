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
    int gi = 4; i64 cf[3] = { 0x7bcda4, 0x7b350b, 0x4ef145 };
    for (int q = 1; q < 3; q++) { int u = 34 - gi - nd[q].e2; printf("node %d (C2f %06llx, unit col %d):\n", nd[q].n, (long long)cf[q], u); for (int j = 0; j < 1024; j++) { i64 t = j - 512; i64 dx = t << 13; i64 PI = arr(cf[q], dx) + K; i64 I = PI >> 24; i64 P2 = arr(I, dx) + K; i64 T2 = sm(P2, u); double fI = (double)(PI & ((1ll << 24) - 1)) / (double)(1ll << 24), f2 = (double)(P2 & ((1ll << u) - 1)) / (double)(1ll << u);
            bool viol = T2 < lo[q][j] || T2 > hi[q][j]; bool tight = (lo[q][j] == hi[q][j]); if (viol) printf("   MISS t=%lld T2=%lld allowed [%lld,%lld]  frac(I)=%.4f frac(T2)=%.5f  I=%lld\n", (long long)t, (long long)T2, (long long)lo[q][j], (long long)hi[q][j], fI, f2, (long long)I);
            else if (tight && (f2 < 0.004 || f2 > 0.996)) printf("   ok   t=%lld T2=%lld (exact)            frac(I)=%.4f frac(T2)=%.5f\n", (long long)t, (long long)T2, fI, f2); } }
    return 0; }
