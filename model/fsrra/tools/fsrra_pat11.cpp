// FSRRA fully structural model (FIPR array for every term), all 34 nodes. Per node: refine the three coefficient fields and C0.
//   T1 = -sm(arr(rec1, t<<4) + K, 26 - e1)                     rec1 = 23-bit field of |C1|
//   T2 = +sm(arr(rec2, sq21) + K, 25 + k2)                     sq21 = (t^2*256 + K) >> 23   (= a^2 >> 15, rounded by K)
//   T3 = -sm(arr(rec3, A3) + K, 29)                            A3 = (arr(sq15, t<<4) + K) >> 20,  sq15 = (t^2*256 + K) >> 29
#include "../../fsca/tools/fsca_common.h"
#include "../sh4_fsrra_table.h"
#include <cmath>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static inline u64 want(uint32_t v, int par) { u64 m = (v & 0x7fffff) | 0x800000; (void)par; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static const i64 K = 1ll << 19;
int main(int argc, char** argv) { int STEP = argc > 1 ? atoi(argv[1]) : 8; const int NA = 1 << 24; std::vector<uint32_t> RZ(NA), RN(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); if (fread(RZ.data(), 4, NA, f) != (size_t)NA) return 1; fclose(f); f = fopen("/tmp/m14.bin", "rb"); if (!f || fread(RN.data(), 4, NA, f) != (size_t)NA) return 1; fclose(f);
    long totMiss = 0, totN = 0; printf("par node | samples misses  (%%)   | rec1 (e1)  rec2 (k2) d2  rec3 d3  dC0\n");
    for (int par = 0; par < 2; par++) for (int n = 0; n < 17; n++) { const int64_t* T = FSRRA_TAB[par * 17 + n]; i64 center = ((i64)par << 23) + ((i64)n << 19);
        std::vector<i64> ts, P1, sq21, A3; std::vector<i64> q4; std::vector<uint8_t> cm; i64 c1 = -T[1]; int e1 = 0; while ((c1 >> e1) >= (1ll << 24)) e1++; i64 rec1base = (c1 >> e1) >> 1; int RD = getenv("RD") ? atoi(getenv("RD")) : 0; i64 rec1 = rec1base + RD;
        for (i64 t = -(1 << 18); t < (1 << 18); t += STEP) { i64 idx = center + t; if (idx < ((i64)par << 23) || idx >= ((i64)(par + 1) << 23)) continue; u64 q = want(RZ[idx], par), rn = want(RN[idx], par); if (rn < q) rn += 0; uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn || (rn == 0x800000 && rne2((i64)(4 * q + r)) == 0x1000000)) m |= 1 << r; if (((RZ[idx] >> 23) & 0xff) != ((RZ[center] >> 23) & 0xff) && n != 0) { } ts.push_back(t); q4.push_back(4 * (i64)q); cm.push_back(m);
            P1.push_back(arr(rec1, t * 16) + K); sq21.push_back((t * t * 256 + K) >> 23); A3.push_back((t * t * 256 + K) >> 25);   /* sq19 */ }
        size_t N = ts.size(); i64 B2 = 4 * T[2]; int k2 = 0; while ((B2 << k2) < (1ll << 22)) k2++; i64 rec2 = B2 << k2, rec3 = T[3] * 128; i64 C0 = T[0]; // result exponent: values in [2^25.5.., 2^26]; for parity 1 the table is in the same 2^-26-style units of the 24-bit mantissa*4
        auto score = [&](i64 r2, i64 r3, i64 c0, int lim) { int miss = 0; for (size_t j = 0; j < N && miss < lim; j++) { i64 W = c0 - sm(P1[j], 26 - e1) + sm(arr(r2, sq21[j]) + K, 25 + k2) - sm(arr(ts[j] * 16, (arr(r3, A3[j]) + K) >> 23) + K, 27); i64 d = W - q4[j]; if (!(d >= 0 && d <= 3 && ((cm[j] >> d) & 1))) miss++; } return miss; };
        int best = score(rec2, rec3, C0, 1 << 30);
        for (int round = 0; round < 3; round++) { for (i64 st : {2048, 512, 128, 32, 8, 2, 1}) for (i64 d = -4 * st; d <= 4 * st; d += st) { int m = score(rec2, rec3 + d, C0, best); if (m < best) { best = m; rec3 += d; } }
            for (i64 st : {16, 4, 1}) for (i64 d = -4 * st; d <= 4 * st; d += st) { int m = score(rec2 + d, rec3, C0, best); if (m < best) { best = m; rec2 += d; } }
            for (i64 d = -2; d <= 2; d++) { int m = score(rec2, rec3, C0 + d, best); if (m < best) { best = m; C0 += d; } } }
        totMiss += best; totN += N; printf(" %d   %2d  | %7zu %6d  (%.3f) | %7lld (%+d) %8lld (%d) %+4lld %8lld %+6lld %+lld\n", par, n, N, best, 100.0 * best / N, (long long)rec1, e1, (long long)rec2, k2, (long long)(rec2 - (B2 << k2)), (long long)rec3, (long long)(rec3 - T[3] * 128), (long long)(C0 - T[0])); fflush(stdout); }
    printf("TOTAL %ld misses of %ld samples (%.4f%%)\n", totMiss, totN, 100.0 * totMiss / totN); return 0; }
