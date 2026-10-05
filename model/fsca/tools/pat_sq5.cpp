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
    for (int side = 0; side < 2; side++) for (int form = 0; form < 3; form++) { double lo = -1e9, hi = 1e9; int tl = 0, th = 0;
        for (int a = 1; a <= 511; a++) { int t = side ? a : -a; Win w = decode(F.srz[16384 + t], F.srn[16384 + t]); i64 V = C0 - T2(t, 0x4ef145); double f = frac(t, 0x4ef145); double g = form == 0 ? 1.0 : form == 1 ? (double)a : (double)a * a;
            // allowed integer shifts j of T2 (V - j in window): excess d must satisfy floor(f + d) == j  ->  j - f <= d < j + 1 - f ; take the union over contiguous allowed j
            int jlo = 99, jhi = -99; for (int j = -2; j <= 2; j++) if (V - j >= w.lo && V - j <= w.hi) { if (j < jlo) jlo = j; if (j > jhi) jhi = j; } if (jlo > jhi) continue;
            double l = (jlo - f) / g, h = (jhi + 1 - f) / g; if (l > lo) { lo = l; tl = t; } if (h < hi) { hi = h; th = t; } }
        printf("side %c excess = s*%s : s in [%.4e, %.4e)  %s   (binding t=%d / t=%d)\n", side ? '+' : '-', form == 0 ? "1" : form == 1 ? "|t|" : "t^2", lo, hi, lo < hi ? "FEASIBLE" : "infeasible", tl, th); }
    return 0; }
