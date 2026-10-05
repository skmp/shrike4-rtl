// FSRRA node 2 + side: FIPR-array placements for the linear (mode L) or cubic (mode C) product, others exact
#include "../../fsca/tools/fsca_common.h"
#include <cmath>
#include <algorithm>
#include <string>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
static inline bool hit(u64 idx, i64 W) { u64 q = want(RZ[idx]); if (W < (i64)(4 * q) || W > (i64)(4 * q + 3)) return false; return ((cand[idx] >> (W - 4 * q)) & 1) != 0; }
int main(int argc, char** argv) { char mode = argv[1][0]; const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    const int n = 2; i64 C0 = 63270842; double C1 = 28120372, B2 = 293088, C3 = 1695.9531, c1 = 0.008240, c2 = 0.000732, c3 = -0.001587;
    struct R { int ok; std::string name; }; std::vector<R> res;
    if (mode == 'L') { uint32_t c1op = (uint32_t)(28120372 >> 1);   // 24-bit
        for (int sp = 0; sp <= 6; sp++) for (int rec = 0; rec < 2; rec++) for (i64 K : {0ll, 1ll << 19}) for (double cc : {-0.02, -0.01, 0.0, 0.008, 0.016, 0.03}) { g_K = K; int ok = 0; int sh = 2 + sp;
            for (i64 t = 0; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; uint32_t top = (uint32_t)(t << sp); i64 p = rec == 0 ? mult28(c1op, top) : mult28(top, c1op); i64 T1 = tz((double)p / (1 << sh) + cc);
                double P2 = B2 * sq / pow(2, 40), P3 = C3 * (double)a * a * a / pow(2, 56); i64 W = C0 - T1 + tz(P2 + c2) - tz(P3 + c3); ok += hit(idx, W); }
            char nm[96]; snprintf(nm, 96, "linear: t<<%d rec %d K %lld c1 %+.3f", sp, rec, (long long)K, cc); res.push_back({ ok, nm }); } }
    else { for (int i3 = 0; i3 <= 13; i3++) for (int j3 = 30; j3 <= 36; j3++) for (int rec = 0; rec < 2; rec++) for (i64 K : {0ll, 1ll << 19}) for (double cc : {-0.004, -0.0016, 0.0, 0.002}) { g_K = K; int ok = 0; int sh = 36 - j3 + i3; uint32_t c3op = 1696u << i3;
            for (i64 t = 0; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; u64 a3 = (u64)a * a * a; uint32_t cu = (uint32_t)(a3 >> j3); i64 p = rec == 0 ? mult28(c3op, cu) : mult28(cu, c3op); i64 T3 = sh >= 0 ? tz((double)p / pow(2, sh) + cc) : tz((double)p * pow(2, -sh) + cc);
                double P1 = C1 * t / pow(2, 23), P2 = B2 * sq / pow(2, 40); i64 W = C0 - tz(P1 + c1) + tz(P2 + c2) - T3; ok += hit(idx, W); }
            char nm[96]; snprintf(nm, 96, "cubic: C3<<%d a3>>%d rec %d K %lld c3 %+.4f", i3, j3, rec, (long long)K, cc); res.push_back({ ok, nm }); } }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.ok > y.ok; }); printf("mode %c (exact-product reference: %d hits of 262144):\n", mode, 262144 - 709); for (int i = 0; i < 10; i++) printf("  %d  %s\n", res[i].ok, res[i].name.c_str()); return 0; }
