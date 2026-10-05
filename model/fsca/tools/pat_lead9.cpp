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
static inline double frm(i64 P, int u) { i64 m = P >= 0 ? P : ~P; return (double)(m & ((1ll << u) - 1)) / (double)(1ll << u); }
int main() { Fsca F; load_fsca(F); ORDER = 0; ROLES = 1; printf("grp node |t| t%%8 | delta bound | T2int  lin frac(+) lin frac(-) cub real | V(+)mod4 V(-)mod4 | x frac\n");
    for (int n = 3; n <= 15; n++) { int m = n; double sv = sin(n * M_PI / 32); int e = 0; while (sv * pow(2, e) < 0.5) e++; i64 S0 = (i64)llround(sv * pow(2, e + 23)); i64 C0 = FSCA_TAB[n][0];
        // S: best fit
        int best = 1 << 30; i64 S = S0; int u = 0; for (i64 Sc = S0 - 8; Sc <= S0 + 8; Sc++) { double lr = 0; int cnt = 0; for (int t = 200; t <= 511; t += 7) { double ex = fabs((double)FSCA_TAB[n][2]) * t * (double)t / 536870912.0; lr += log2((double)sqprod(t, Sc) / ex); cnt++; } int uu = (int)llround(lr / cnt); int miss = 0; for (int t = -512; t <= 511; t++) { Win w = decode(F.srz[n * 1024 + t], F.srn[n * 1024 + t]); i64 dx = (i64)t << 13; i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; i64 V = C0 + sm(arr(M1[m] >> 1, dx) + K, 23 - E1[m]) - sm(sqprod(t, Sc), uu) - sm(arr(C3g[m] << 5, A3) + K, 26); if (V < w.lo || V > w.hi) miss++; } if (miss < best) { best = miss; S = Sc; u = uu; } }
        for (int a = 1; a <= 511; a++) { double L = -9, H = 9; i64 vm[2] = { 0, 0 }; double lf[2] = { 0, 0 }; double cubr = 0; for (int q = 0; q < 2; q++) { int t = q ? -a : a; Win w = decode(F.srz[n * 1024 + t], F.srn[n * 1024 + t]); i64 dx = (i64)t << 13; i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; i64 P1 = arr(M1[m] >> 1, dx) + K, P3 = arr(C3g[m] << 5, A3) + K; i64 rest = C0 + sm(P1, 23 - E1[m]) - sm(P3, 26); double x = (double)sqprod(t, S) / pow(2, u); double dl = (double)(rest - w.hi) - x, dh = (double)(rest - w.lo + 1) - x; if (dl > L) L = dl; if (dh < H) H = dh; vm[q] = w.lo & 3; lf[q] = frm(P1, 23 - E1[m]); cubr = fabs((double)P3 / pow(2, 26)); }
            double x = (double)sqprod(a, S) / pow(2, u); if (H < -0.004) printf(" D  %2d  %3d  %d | delta < %+.4f | %6lld  %.3f %.3f  %7.3f | %lld %lld | %.4f\n", n, a, a & 7, H, (long long)x, lf[0], lf[1], cubr, (long long)vm[0], (long long)vm[1], x - floor(x)); else if (L > 0.004) printf(" U  %2d  %3d  %d | delta > %+.4f | %6lld  %.3f %.3f  %7.3f | %lld %lld | %.4f\n", n, a, a & 7, L, (long long)x, lf[0], lf[1], cubr, (long long)vm[0], (long long)vm[1], x - floor(x)); else if (L > -0.004 && H > 0.5) printf(" n- %2d  %3d  %d | delta > %+.4f | %6lld  %.3f %.3f  %7.3f | %lld %lld | %.4f\n", n, a, a & 7, L, (long long)x, lf[0], lf[1], cubr, (long long)vm[0], (long long)vm[1], x - floor(x)); else if (H < 0.004 && L < -0.5) printf(" n+ %2d  %3d  %d | delta < %+.4f | %6lld  %.3f %.3f  %7.3f | %lld %lld | %.4f\n", n, a, a & 7, H, (long long)x, lf[0], lf[1], cubr, (long long)vm[0], (long long)vm[1], x - floor(x)); } }
    return 0; }
