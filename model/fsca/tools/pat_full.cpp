// Full-table score of the patent-derived array model. Coefficients: C0 from fsca_table (+-1), C1 = RN23 mantissas, C3 = 12-bit grid,
// C2 = 23-bit field fitted per node (+-10). Coefficient signs handled sign-magnitude (FIPR sign logic); second half turn = negation.
#include "fsca_common.h"
#include "../archive/fsca_ref.h"
#include <vector>
#include <cmath>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19;
static const i64 M1[17] = { 0xc90fda, 0xc81800, 0xc532d6, 0xc0677e, 0xb9c1ca, 0xb15220, 0xa72d48, 0x9b6c48, 0x8e2c18, 0xff1acc, 0xdf6876, 0xbd8f54, 0x99e2da, 0xe975f6, 0x9ce6a4, 0x9da8fe, 0 };
static const int E1[17] = { 0,0,0,0,0,0,0,0,0,-1,-1,-1,-1,-2,-2,-3, 0 };
static const i64 C3g[17] = { 0xa56, 0xa49, 0xa23, 0x9e4, 0x98c, 0x91d, 0x898, 0x7fd, 0x74f, 0x68f, 0x5be, 0x4df, 0x3f5, 0x300, 0x204, 0x103, 0 };
static i64 model(int n, int t, i64 C0, i64 c2f, int e2) { int m = n <= 16 ? n : 32 - n; int s1 = n <= 16 ? 1 : -1; i64 dx = (i64)t << 13;
    i64 T1 = M1[m] ? s1 * sm(arr(M1[m] >> 1, dx) + K, 23 - E1[m]) : 0; i64 T2 = c2f ? sm(arr(c2f, ((i64)t * t) << 2) + K, 29 - 1 - e2 + 2) : 0;
    i64 T3 = 0; if (C3g[m]) { i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; T3 = s1 * sm(arr(C3g[m] << 5, A3) + K, 26); } return C0 + T1 - T2 - T3; }
int main() { Fsca F; load_fsca(F); int totRN = 0, totRZ = 0, refRN = 0, refRZ = 0; printf("node  C0adj C2field e2 | misses RN RZ (of 2048 incl. mirror half-turn) | old ref RN RZ\n");
    for (int n = 0; n <= 32; n++) { i64 C2t = llabs(FSCA_TAB[n][2]); int e2 = 0; i64 f = C2t; while (f >= (1ll << 24)) { f >>= 1; e2++; } i64 f23 = f >> 1; int best = 1 << 30, bRN = 0, bRZ = 0; i64 bc = 0; int bd = 0;
        for (int d0 = -1; d0 <= 1; d0++) for (i64 c = C2t ? f23 - 12 : 0; c <= (C2t ? f23 + 12 : 0); c++) { int rn = 0, rz = 0; for (int t = -512; t <= 511; t++) { for (int half = 0; half < 2; half++) { int idx = (n * 1024 + t + half * 32768 + 65536) % 65536; if (n == 32 && t >= 0 && half == 0) { } i64 V = model(n, t, FSCA_TAB[n][0] + d0, c, e2); if (half) V = -V; rn += fsca_to_float(V, 0) != F.srn[idx]; rz += fsca_to_float(V, 1) != F.srz[idx]; } } if (rn + rz < best) { best = rn + rz; bRN = rn; bRZ = rz; bc = c; bd = d0; } }
        int orn = 0, orz = 0; for (int t = -512; t <= 511; t++) for (int half = 0; half < 2; half++) { int idx = (n * 1024 + t + half * 32768 + 65536) % 65536; orn += fsca_sin(idx, 0) != F.srn[idx]; orz += fsca_sin(idx, 1) != F.srz[idx]; }
        if (n > 0 && n < 32) { totRN += bRN; totRZ += bRZ; refRN += orn; refRZ += orz; } else { totRN += bRN / 1; totRZ += bRZ; refRN += orn; refRZ += orz; }
        printf("%2d    %+d   %06llx  %d | %4d %4d | %4d %4d\n", n, bd, (long long)bc, e2, bRN, bRZ, orn, orz); }
    printf("TOTAL new model RN %d RZ %d   old reference RN %d RZ %d   (nodes 0 and 32 each counted over their full +-512 range)\n", totRN, totRZ, refRN, refRZ); return 0; }
