#include "fsca_common.h"
#include <vector>
#include <cmath>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
int main() { Fsca F; load_fsca(F); const i64 K = 1ll << 19, C0 = 67108863;
    // reference symmetric config: KA=1 g=3 K5=1 b=22 S=0x7fffff u=28 kc=0x4ef145
    auto T2 = [&](int t, i64 kc) { i64 tf = (i64)t << 13; i64 MA = (arr(tf, tf) + K) >> 20; i64 Af = (MA >> 3) & 0xffffff; i64 y5 = (arr(kc, Af) + K) >> 22; return sm(arr(y5, 0x7fffff) + K, 28); };
    auto frac = [&](int t, i64 kc) { i64 tf = (i64)t << 13; i64 MA = (arr(tf, tf) + K) >> 20; i64 Af = (MA >> 3) & 0xffffff; i64 y5 = (arr(kc, Af) + K) >> 22; i64 p = arr(y5, 0x7fffff) + K; return (double)(p & ((1ll << 28) - 1)) / (double)(1ll << 28); };
    printf("negative side, fraction of the final product (symmetric model); an excess d on this side must satisfy frac+d>=1 for LOWER and frac+d<1 for EQUAL\n");
    double needmin = 0, allowmax = 1; for (int a = 1; a <= 512; a++) { int t = -a; Win w = decode(F.srz[16384 + t], F.srn[16384 + t]); i64 V = C0 - T2(t, 0x4ef145); bool f0 = V >= w.lo && V <= w.hi, fm = V - 1 >= w.lo && V - 1 <= w.hi; double f = frac(t, 0x4ef145);
        if (fm && !f0) { printf("  LOWER t=%d frac=%.5f -> needs d >= %.5f   (d/|t| = %.3e, d/t^2 = %.3e)\n", t, f, 1 - f, (1 - f) / a, (1 - f) / ((double)a * a)); if (1 - f > needmin) needmin = 1 - f; }
        else if (f0 && !fm) { if (1 - f < allowmax) { allowmax = 1 - f; printf("  EQUAL t=%d frac=%.5f -> needs d < %.5f   (d/|t| < %.3e, d/t^2 < %.3e)\n", t, f, 1 - f, (1 - f) / a, (1 - f) / ((double)a * a)); } } }
    printf("constant excess feasible iff %.5f <= d < %.5f\n", needmin, allowmax); return 0; }
