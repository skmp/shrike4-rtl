#include "fsca_common.h"
#include <cmath>
static i64 g_jam = 0;   // 0 none, 1 OR 1, 2 add 1
static inline i64 mult28s(uint32_t rec24, uint32_t mul, i64 K) { u64 R[13]; i64 T = 0; uint32_t x = rec24 & 0xffffff; if (x & 0x800000) x |= 0x3000000;
    for (int k = 0; k < 13; k++) { int d = booth_digit(x, k); u64 mag = (u64)(d < 0 ? -d : d) * mul; u64 oc = d < 0 ? (~mag & ((1ull << 25) - 1)) : mag; R[k] = oc << (2*k); T += (i64)((R[k] >> 16) << 16); if (d < 0) { if (2*k >= 18) T += 1ll << (2*k); T -= 1ll << (25 + 2*k); } }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16;
    i64 p = (T + K) >> 20; if (g_jam == 1) p |= 1; else if (g_jam == 2) p += 1; return p; }
static inline i64 tzs(i64 p, int sh) { return p < 0 ? -((-p) >> sh) : (p >> sh); }
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1 = (i64)floor(k * pow(2, 37)); i64 V[2][512];
    for (int a = 1; a <= 511; a++) { Win wp = decode(F.srz[a], F.srn[a]), wm = decode(F.srz[65536 - a], F.srn[65536 - a]); V[0][a] = wp.sign * wp.lo; V[1][a] = wm.sign * wm.lo; }
    // cubic: signed too: P3 = C3' * t'^3 with C3' = 0xA56<<2 recoded, t'^3>>4 as signed multiplicand? multiplicand is unsigned in the array -> use magnitude and apply sign to the product, constant then trunc toward zero
    for (int jam = 0; jam < 3; jam++) for (i64 K : {1ll << 19, (1ll << 19) + (1ll << 20), (1ll << 19) + (1ll << 18), 5ll << 18}) { g_jam = jam; int mism = 0, mp = 0, mm = 0;
        for (int a = 1; a <= 511; a++) for (int si = 0; si < 2; si++) { int s = si ? -1 : 1; uint32_t rec = (uint32_t)((s * a) << 14) & 0xffffff; i64 p1 = mult28s(rec, (uint32_t)m1, K); i64 T1 = tzs(p1, 5);
            u64 a3 = (u64)a * a * a; i64 p3 = mult28s(0xA56u << 2, (uint32_t)(a3 >> 4), K); i64 P3 = -s * p3; i64 T3 = tzs(P3 + (jam ? 0 : 0), 6);   // unit 2^26 for this cubic placement
            // constant handling for the cubic: the array constant K is added to |p3| before sign (as computed); magnitude truncation
            i64 W = T1 + T3; if (W != V[si][a]) { mism++; (si ? mm : mp)++; } }
        printf("jam %d K=%lld: mism %d (+%d/-%d)\n", jam, (long long)K, mism, mp, mm); }
    return 0; }
