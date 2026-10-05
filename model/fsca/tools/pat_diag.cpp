// Whole-dump score with the chained square term: I1 = (arr(Kc, d) [roles] + K) >> 23 ; then x d and x S (order option), g2 = 0.
#include "fsca_common.h"
#include "../archive/fsca_ref.h"
#include <vector>
#include <cmath>
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19, KC = 0x4ef145;
static const i64 M1[17] = { 0xc90fda, 0xc81800, 0xc532d6, 0xc0677e, 0xb9c1ca, 0xb15220, 0xa72d48, 0x9b6c48, 0x8e2c18, 0xff1acc, 0xdf6876, 0xbd8f54, 0x99e2da, 0xe975f6, 0x9ce6a4, 0x9da8fe, 0 };
static const int E1[17] = { 0,0,0,0,0,0,0,0,0,-1,-1,-1,-1,-2,-2,-3, 0 };
static const i64 C3g[17] = { 0xa56, 0xa49, 0xa23, 0x9e4, 0x98c, 0x91d, 0x898, 0x7fd, 0x74f, 0x68f, 0x5be, 0x4df, 0x3f5, 0x300, 0x204, 0x103, 0 };
static int ORDER = 0, ROLES = 0;
static i64 sqprod(int t, i64 S) { i64 d = (i64)t << 13; i64 I = (((ROLES & 1) ? arr(d, KC) : arr(KC, d)) + K) >> 23; i64 b = ORDER == 0 ? d : S; I = (((ROLES & 2) ? arr(b, I) : arr(I, b)) + K) >> 20; b = ORDER == 0 ? S : d; return ((ROLES & 4) ? arr(b, I) : arr(I, b)) + K; }
static inline double fr(i64 P, int u) { i64 m = P >= 0 ? P : ~P; return (double)(m & ((1ll << u) - 1)) / (double)(1ll << u); }
int main() { Fsca F; load_fsca(F); ORDER = 1; ROLES = 4;
    for (int n : {3, 5, 9, 13}) { int m = n; i64 C0 = FSCA_TAB[n][0]; i64 mm = C0; while (mm >= (1ll << 24)) mm >>= 1; i64 S = mm >> 1; double lr = 0; int cnt = 0; for (int t = 200; t <= 511; t += 7) { double ex = fabs((double)FSCA_TAB[n][2]) * t * (double)t / 536870912.0; lr += log2((double)sqprod(t, S) / ex); cnt++; } int u = (int)llround(lr / cnt);
        int h1[2][10] = {}, h2[2][10] = {}, h3[2][10] = {}; int miss = 0, up = 0, dn = 0, far = 0, side[2][2] = {}; int bt[2][4] = {};
        for (int t = -512; t <= 511; t++) { Win w = decode(F.srz[n * 1024 + t], F.srn[n * 1024 + t]); i64 dx = (i64)t << 13; i64 P1 = arr(M1[m] >> 1, dx) + K, P2 = sqprod(t, S); i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; i64 P3 = arr(C3g[m] << 5, A3) + K; i64 V = C0 + sm(P1, 23 - E1[m]) - sm(P2, u) - sm(P3, 26);
            if (V >= w.lo && V <= w.hi) continue; miss++; int dir = V < w.lo ? 1 : -1; i64 dist = V < w.lo ? w.lo - V : V - w.hi; if (dist > 1) far++; if (dir > 0) up++; else dn++; side[dir > 0][t >= 0]++; bt[dir > 0][(t < 0 ? -t : t) >> 7]++; h1[dir > 0][(int)(fr(P1, 23 - E1[m]) * 10)]++; h2[dir > 0][(int)(fr(P2, u) * 10)]++; h3[dir > 0][(int)(fr(P3, 26) * 10)]++; }
        printf("node %d: %d window violations; hw higher %d (t<0 %d, t>=0 %d), hw lower %d (t<0 %d, t>=0 %d), off by >1: %d\n", n, miss, up, side[1][0], side[1][1], dn, side[0][0], side[0][1], far);
        for (int k = 1; k >= 0; k--) { printf("  %s | by |t|/128:", k ? "hw higher" : "hw lower "); for (int b = 0; b < 4; b++) printf(" %d", bt[k][b]); printf(" | lin frac:"); for (int b = 0; b < 10; b++) printf(" %d", h1[k][b]); printf(" | sq frac:"); for (int b = 0; b < 10; b++) printf(" %d", h2[k][b]); printf(" | cub frac:"); for (int b = 0; b < 10; b++) printf(" %d", h3[k][b]); printf("\n"); } }
    return 0; }
