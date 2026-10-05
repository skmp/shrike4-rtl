// Per-node fit of the second latch shift g2 (I2 = (arr(I1,dx)+K) >> (20+g2)) and of a shift applied to the sine operand, S fitted.
#include "fsca_common.h"
#include "../fsca_table.h"
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
int main() { Fsca F; load_fsca(F); printf("node | violations by (g1, g2): rows g1 = 1..5, columns g2 = -3..2 (S refitted each)\n");
    for (int n : {3, 5, 9, 13}) { double sv = sin(n * M_PI / 32); int e = 0; while (sv * pow(2, e) < 0.5) e++; i64 S0 = (i64)llround(sv * pow(2, e + 23)); std::vector<i64> lo(1024), hi(1024);
        for (int j = 0; j < 1024; j++) { int t = j - 512; Win w = decode(F.srz[n * 1024 + t], F.srn[n * 1024 + t]); i64 dx = (i64)t << 13; i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; i64 base = FSCA_TAB[n][0] + sm(arr(M1[n] >> 1, dx) + K, 23 - E1[n]) - sm(arr(C3g[n] << 5, A3) + K, 26); lo[j] = base - w.hi; hi[j] = base - w.lo; }
        printf("node %d\n", n); for (int g1 = 1; g1 <= 5; g1++) { printf("   g1=%d:", g1); for (int g2 = -3; g2 <= 2; g2++) { int best = 1 << 30; std::vector<i64> I2(1024); bool ovf = false; for (int j = 0; j < 1024; j++) { i64 d = (i64)(j - 512) << 13; i64 I1 = (arr(KC, d) + K) >> (20 + g1); I2[j] = (arr(I1, d) + K) >> (20 + g2); if (I2[j] >= (1ll << 26)) ovf = true; }
                if (ovf) { printf("   -"); continue; } for (i64 S = S0 - 10; S <= S0 + 10; S++) { double lr = 0; int cnt = 0; for (int j = 712; j < 1024; j += 7) { int t = j - 512; double ex = fabs((double)FSCA_TAB[n][2]) * t * (double)t / 536870912.0; lr += log2((double)(arr(I2[j], S) + K) / ex); cnt++; } int u = (int)llround(lr / cnt); int miss = 0; for (int j = 0; j < 1024 && miss < best; j++) { i64 T2 = sm(arr(I2[j], S) + K, u); if (T2 < lo[j] || T2 > hi[j]) miss++; } if (miss < best) best = miss; }
                printf(" %3d", best); } printf("\n"); } }
    return 0; }
