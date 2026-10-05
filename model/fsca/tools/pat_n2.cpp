#include "fsca_common.h"
#include <vector>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19;
int main() { Fsca F; load_fsca(F); const int g2 = 2, ga = 3, g3 = 3;
    for (i64 c2 = 0x7b350c; c2 <= 0x7b3510; c2++) { int miss = 0; printf("C2 field %06llx:", (long long)c2);
        for (int t = -512; t <= 511; t++) { Win w = decode(F.srz[2048 + t], F.srn[2048 + t]); i64 V = w.lo; i64 T1 = sm(arr(0xc532d6 >> 1, (i64)t << 13) + K, 23); i64 P2 = arr(c2, ((i64)t * t) << g2) + K; i64 T2 = sm(P2, 29 - 1 - 1 + g2); i64 A3 = ((arr(((i64)t * t) << ga, (i64)t << 13) + K) >> 20) >> g3; i64 P3 = arr(0xa23 << 5, A3) + K; i64 T3 = sm(P3, 26); i64 M = 13092290 + T1 - T2 - T3;
            if (M != V) { miss++; int u2 = 29 - 2 + g2; printf("  [t=%d hw-model=%lld  sq frac=%.4f  cub frac=%.4f]", t, (long long)(V - M), (double)(P2 & ((1ll << u2) - 1)) / (double)(1ll << u2), (double)((P3 < 0 ? ~P3 : P3) & ((1ll << 26) - 1)) / (double)(1ll << 26)); } }
        printf("  -> %d misses\n", miss); }
    return 0; }
