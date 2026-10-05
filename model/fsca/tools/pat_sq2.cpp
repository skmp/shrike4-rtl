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
    printf("violations of the symmetric reference:\n"); for (int t = -512; t <= 511; t++) { if (!t) continue; Win w = decode(F.srz[16384 + t], F.srn[16384 + t]); i64 V = C0 - T2(t, 0x4ef145); if (V < w.lo || V > w.hi) { Win w2 = decode(F.srz[16384 - t], F.srn[16384 - t]); printf("  t=%d model V=%lld window [%lld,%lld]   (mirror t=%d window [%lld,%lld])\n", t, (long long)V, (long long)w.lo, (long long)w.hi, -t, (long long)w2.lo, (long long)w2.hi); } }
    // add a tiny linear term: T1 = sm(arr(c, t<<13) + K, u)
    printf("adding T1 = sm(arr(c, t<<13)+2^19, u):  c  u  kc  violations\n"); int best = 99;
    for (i64 kc = 0x4ef140; kc <= 0x4ef14a; kc++) for (int c = -40; c <= 40; c++) for (int u = 20; u <= 30; u++) { int v = 0; for (int t = -512; t <= 511 && v <= best; t++) { if (!t) continue; Win w = decode(F.srz[16384 + t], F.srn[16384 + t]); i64 V = C0 - T2(t, kc) + sm(arr(c, (i64)t << 13) + K, u); if (V < w.lo || V > w.hi) v++; } if (v < best || (v == best && v == 0)) { best = v; printf("   c=%d u=%d kc=%06llx -> %d\n", c, u, (long long)kc, v); } }
    return 0; }
