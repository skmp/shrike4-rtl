// Square operand offset: A = 64*t^2 - round(e * t / 64)   (i.e. (t - e*2^-13)^2 at 6 fraction bits), T2 = sm(arr(C2f, A) + K, u).
#include "fsca_common.h"
#include <vector>
#include <cmath>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19;
struct Node { int n; i64 C0, M1; int e1; i64 C2f; int e2; i64 C3; };
int main() { Fsca F; load_fsca(F); Node nd[3] = { { 1, 6577819, 0xc81800, 0, 0x7bcdae, 0, 0xa49 }, { 2, 13092290, 0xc532d6, 0, 0x7b350e, 1, 0xa23 }, { 16, 67108863, 0, 0, 0x4ef144, 4, 0 } };
    printf("  e    | node1 (best C2f) | node2 (best C2f) | node16 viol (best C2f)\n");
    for (double e = -4; e <= 4.001; e += 0.25) { printf("%+5.2f |", e);
        for (int q = 0; q < 3; q++) { int best = 1 << 30; i64 bc = 0; for (i64 c2 = nd[q].C2f - 6; c2 <= nd[q].C2f + 6; c2++) { int miss = 0;
                for (int t = -512; t <= 511; t++) { int idx = nd[q].n * 1024 + t; Win w = decode(F.srz[idx], F.srn[idx]); i64 T1 = nd[q].M1 ? sm(arr(nd[q].M1 >> 1, (i64)t << 13) + K, 23 - nd[q].e1) : 0; i64 A = 64ll * t * t - (i64)llround(e * t / 64.0); if (A < 0) A = 0; if (A >= (1ll << 25)) A = (1ll << 25) - 1;
                    i64 T2 = sm(arr(c2, A) + K, 29 - 1 - nd[q].e2 + 6); i64 T3 = 0; if (nd[q].C3) { i64 A3 = ((arr(((i64)t * t) << 3, (i64)t << 13) + K) >> 20) >> 3; T3 = sm(arr(nd[q].C3 << 5, A3) + K, 26); } i64 M = nd[q].C0 + T1 - T2 - T3; if (M < w.lo || M > w.hi) miss++; }
                if (miss < best) { best = miss; bc = c2; } }
            printf("  %3d (%06llx)   |", best, (long long)bc); }
        printf("\n"); }
    return 0; }
