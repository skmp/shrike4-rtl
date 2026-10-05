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
int main() { Fsca F; load_fsca(F); ORDER = 0; ROLES = 1; int pair_same = 0, pair_opp = 0, single_mirror_ok_tight = 0, single_mirror_loose = 0; int byres[8][2] = {}; int total = 0;
    for (int n = 3; n <= 15; n++) { int m = n; double sv = sin(n * M_PI / 32); int e = 0; while (sv * pow(2, e) < 0.5) e++; i64 S = (i64)llround(sv * pow(2, e + 23)); i64 C0 = FSCA_TAB[n][0]; double lr = 0; int cnt = 0; for (int t = 200; t <= 511; t += 7) { double ex = fabs((double)FSCA_TAB[n][2]) * t * (double)t / 536870912.0; lr += log2((double)sqprod(t, S) / ex); cnt++; } int u = (int)llround(lr / cnt);
        auto dir = [&](int t, bool& tight) { Win w = decode(F.srz[n * 1024 + t], F.srn[n * 1024 + t]); i64 dx = (i64)t << 13; i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; i64 V = C0 + sm(arr(M1[m] >> 1, dx) + K, 23 - E1[m]) - sm(sqprod(t, S), u) - sm(arr(C3g[m] << 5, A3) + K, 26); tight = (w.hi - w.lo) <= 1; return V < w.lo ? 1 : V > w.hi ? -1 : 0; };
        for (int a = 1; a <= 511; a++) { bool t1, t2; int d1 = dir(a, t1), d2 = dir(-a, t2); if (!d1 && !d2) continue; if (d1) { total++; byres[a & 7][d1 > 0]++; } if (d2) { total++; byres[a & 7][d2 > 0]++; } if (d1 && d2) { if (d1 == d2) pair_same++; else pair_opp++; } else { bool mt = d1 ? t2 : t1; if (mt) single_mirror_ok_tight++; else single_mirror_loose++; } } }
    printf("violations %d: symmetric pairs same direction %d, opposite direction %d, single with a TIGHT mirror that passes %d, single with a loose mirror %d\n", total, pair_same, pair_opp, single_mirror_ok_tight, single_mirror_loose);
    printf("by |t| mod 8 (hw lower / hw higher):"); for (int r = 0; r < 8; r++) printf("  %d: %d/%d", r, byres[r][0], byres[r][1]); printf("\n"); return 0; }
