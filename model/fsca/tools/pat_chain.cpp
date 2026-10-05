// Square as a coefficient-first chain through the same array: B = (arr(C2f, dx) + K) >> 20 ; Bf = B >> gB (floor or one's-complement magnitude)
// T2 = sm(arr(Bf, dx) + K, u)  or roles swapped.   dx = t << 13.   Nodes 1, 2 (exact) and 16 (windows).
#include "fsca_common.h"
#include <vector>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19;
struct Node { int n; i64 C0, M1; int e1; i64 C2f; int e2; i64 C3; };
int main() { Fsca F; load_fsca(F); Node nd[3] = { { 1, 6577819, 0xc81800, 0, 0x7bcdae, 0, 0xa49 }, { 2, 13092290, 0xc532d6, 0, 0x7b350e, 1, 0xa23 }, { 16, 67108863, 0, 0, 0x4ef144, 4, 0 } };
    printf("gB Bmode roles K1st | node1 | node2 | node16   (misses/violations, best C2 field)\n");
    for (int gB = 0; gB <= 8; gB++) for (int bm = 0; bm < 2; bm++) for (int roles = 0; roles < 2; roles++) for (int k1 = 0; k1 < 2; k1++) { int tot = 0; char line[256]; int pos = snprintf(line, 256, "%d    %d     %d     %d   |", gB, bm, roles, k1);
        for (int q = 0; q < 3; q++) { int best = 1 << 30; i64 bc = 0; for (i64 c2 = nd[q].C2f - 8; c2 <= nd[q].C2f + 8; c2++) { int miss = 0; int u = 34 - gB - nd[q].e2;
                for (int t = -512; t <= 511 && miss < best; t++) { int idx = nd[q].n * 1024 + t; Win w = decode(F.srz[idx], F.srn[idx]); i64 dx = (i64)t << 13; i64 T1 = nd[q].M1 ? sm(arr(nd[q].M1 >> 1, dx) + K, 23 - nd[q].e1) : 0;
                    i64 PB = arr(c2, dx) + (k1 ? K : 0); i64 Bf = bm == 0 ? (PB >> (20 + gB)) : sm(PB, 20 + gB); i64 P2 = (roles == 0 ? arr(Bf, dx) : arr(dx, Bf)) + K; i64 T2 = sm(P2, u);
                    i64 T3 = 0; if (nd[q].C3) { i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; T3 = sm(arr(nd[q].C3 << 5, A3) + K, 26); } i64 M = nd[q].C0 + T1 - T2 - T3; if (M < w.lo || M > w.hi) miss++; }
                if (miss < best) { best = miss; bc = c2; } }
            tot += best; pos += snprintf(line + pos, 256 - pos, " %4d (%06llx) |", best, (long long)bc); }
        if (tot <= 12) printf("%s  total %d\n", line, tot); }
    return 0; }
