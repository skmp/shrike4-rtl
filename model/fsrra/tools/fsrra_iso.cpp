#include "../../fsca/tools/fsca_common.h"
#include <cmath>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
int main() { const int NA = 1 << 18; std::vector<uint32_t> RZ(NA), RN(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    std::vector<uint8_t> cand(NA); for (int a = 0; a < NA; a++) { u64 q = want(RZ[a]), rn = want(RN[a]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[a] = m; }
    auto hit = [&](int a, i64 W) { u64 q = want(RZ[a]); if (W < (i64)(4 * q) || W > (i64)(4 * q + 3)) return false; return ((cand[a] >> (W - 4 * q)) & 1) != 0; };
    std::vector<i64> T3(NA); for (int a = 0; a < NA; a++) T3[a] = (i64)floor(640.0 * a * (double)a * a / pow(2, 54));
    for (int j : {12, 13, 14, 15, 16}) { int ok = 0; for (int a = 1; a < NA; a++) { u64 sq = ((u64)a * a) >> j; i64 T2 = (i64)(((u64)24592 * sq) >> (36 - j)); i64 W = (1 << 26) - 4 * a + T2 - T3[a]; ok += hit(a, W); } printf("exact product with sq = a^2>>%d: %d\n", j, ok); }
    for (i64 K : {0ll, 1ll << 18, 1ll << 19, 1ll << 20}) for (int i : {7, 8, 9}) { g_K = K; int ok = 0; for (int a = 1; a < NA; a++) { uint32_t sq = (uint32_t)(((u64)a * a) >> 15); i64 p = mult28(0x6010u << i, sq); i64 T2 = p >> (1 + i); i64 W = (1 << 26) - 4 * a + T2 - T3[a]; ok += hit(a, W); } printf("array sq=a^2>>15, C2<<%d, K=%lld: %d\n", i, (long long)K, ok); }
    // array without group terms / hot-one loss: plain floor rows (emulate by exact product + K then truncate)
    for (i64 K : {1ll << 19}) { int ok = 0; for (int a = 1; a < NA; a++) { u64 sq = ((u64)a * a) >> 15; i64 p = ((i64)((0x6010u << 8) * sq) + K) >> 20; i64 T2 = p >> 9; i64 W = (1 << 26) - 4 * a + T2 - T3[a]; ok += hit(a, W); } printf("exact P48 + 2^19 then >>29 (no row truncation), C2<<8: %d\n", ok); }
    return 0; }
